classdef EmuFixture < matlab.unittest.fixtures.Fixture
    %EMUFIXTURE A fresh temporary directory with the emulated IIO context of the tests.
    %   The directory holds a copy of tests/resources/iio_fixture.xml and the receive data
    %   file emu-rx_buf0.bin (the ramp of EmuFixture.rampWords(4096)). It is removed at
    %   teardown.
    %
    %   A frame or block read from emu must not be longer than that file: 16384 bytes,
    %   which is SamplesPerFrame = 4096 with two channels and 2048 with four. The emu
    %   backend fills a longer block only in part and still reports it as valid (the
    %   rest is not data). Every test therefore passes SamplesPerFrame; the default of
    %   iio.RxStream, 32768, is for hardware.

    properties (SetAccess = private)
        Dir = ''        % char: the temporary directory (absolute)
        XmlFile = ''    % char: <Dir>/iio_fixture.xml
        Uri = ''        % char: ['emu:' XmlFile]
        XmlUri = ''     % char: ['xml:' XmlFile]
    end

    methods
        function setup(fixture)
            % A missing MEX file must fail every test, not skip it.
            assert(exist('iio_mex', 'file') == 3, 'EmuFixture:noMex', ...
                'iio_mex is not a MEX file on the path: build it and add the folder "matlab".');
            src = fullfile(fileparts(mfilename('fullpath')), '..', '..', 'tests', 'resources', 'iio_fixture.xml');
            assert(isfile(src), 'EmuFixture:noXml', 'Fixture file not found: %s', src);
            d = tempname;
            [ok, msg] = mkdir(d);
            assert(ok, 'EmuFixture:mkdir', 'Cannot create %s: %s', d, msg);
            fixture.addTeardown(@removeDir, d);
            fixture.Dir = d;
            fixture.XmlFile = fullfile(d, 'iio_fixture.xml');
            fixture.Uri = ['emu:' fixture.XmlFile];
            fixture.XmlUri = ['xml:' fixture.XmlFile];
            copyfile(src, fixture.XmlFile);
            fixture.writeWords('emu-rx_buf0.bin', EmuFixture.rampWords(4096));
        end

        function writeWords(fixture, file, words)
            %WRITEWORDS Write WORDS as little-endian uint16 to <Dir>/<file>.
            name = fullfile(fixture.Dir, file);
            fid = fopen(name, 'w');
            assert(fid >= 0, 'EmuFixture:open', 'Cannot write %s', name);
            n = fwrite(fid, words, 'uint16', 0, 'ieee-le');
            fclose(fid);
            assert(n == numel(words), 'EmuFixture:write', 'Short write to %s', name);
        end

        function v = readInt16(fixture, file)
            %READINT16 Content of <Dir>/<file> as an int16 column (little-endian).
            name = fullfile(fixture.Dir, file);
            fid = fopen(name, 'r');
            if fid < 0
                error('EmuFixture:open', 'Cannot read %s', name);
            end
            v = fread(fid, Inf, 'int16=>int16', 0, 'ieee-le');
            fclose(fid);
            v = v(:);
        end

        function ctx = fakeContext(fixture)
            %FAKECONTEXT Context on the test-only fake backend (needs a MEX built with the test hooks).
            ctx = iio.Context(AdoptHandle = iio_mex('test_create_fake_context', fixture.XmlFile));
        end
    end

    methods (Static)
        function w = rampWords(nbSamples)
            %RAMPWORDS uint16 row of 2*nbSamples words W(0), W(1), ... of the receive ramp.
            %   Sample k (taken modulo 4096): I = k - 2048, Q = 2047 - k, each in the low
            %   12 bits of its word; the upper four bits hold bits 3..0 (I) and 7..4 (Q) of k.
            k = mod(0:nbSamples - 1, 4096);
            w = zeros(1, 2 * nbSamples, 'uint16');
            w(1:2:end) = mod(k - 2048, 4096) + 4096 * mod(k, 16);
            w(2:2:end) = mod(2047 - k, 4096) + 4096 * mod(floor(k / 16), 16);
        end

        function v = s12(words)
            %S12 int16, same size as WORDS: the low 12 bits of each word, sign-extended.
            v = mod(double(words), 4096);
            v(v >= 2048) = v(v >= 2048) - 4096;
            v = int16(v);
        end

        function v = s14shift2(words)
            %S14SHIFT2 int16, same size as WORDS: bits 15..2 of each word, sign-extended.
            v = floor(double(words) / 4);
            v(v >= 8192) = v(v >= 8192) - 16384;
            v = int16(v);
        end
    end
end

function removeDir(d)
[~, ~] = rmdir(d, 's');   % with outputs: no error and no warning if it fails
end
