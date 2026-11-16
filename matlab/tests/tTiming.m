classdef tTiming < IioTestCase
    %TTIMING Timing report: one frame from iio.RxStream against the same frame built in M code.
    %   The test asserts only that the paths it compares deliver the same data. The
    %   times are reported, never asserted: in the test log and in the file
    %   timing.txt of the folder named by the environment variable IIO_TEST_RESULTS
    %   (tempdir when it is not set).
    %
    %   A frame is 262144 samples of two 16-bit channels, I and Q, read from emu-rx.
    %   Every time includes the read of the frame from the data file of the emu backend.
    %
    %   Lines of timing.txt, one per path: <path> <median ms per frame> <MS/s>
    %       fast_int16_complex      RxStream, OutputDataType "int16", Complex
    %       mcode_int16_complex     RxStream "int16", Raw, real (N-by-2), then in M code
    %                               the transpose to the 2-by-N layout a receiver class
    %                               of the toolboxes gets its data in, complex() of the
    %                               two rows and the transpose back
    %       fast_double_fullscale   RxStream, OutputDataType "double", Complex,
    %                               Scale "fullscale"
    %       mcode_double_fullscale  mcode_int16_complex, then double(x) / 2048
    %   and two lines "ratio <M-code path>/<fast path> <quotient of the two medians>".

    methods (Test)
        function fastPathVersusMCode(tc)
            N = 262144;         % samples per frame
            nbWarmUp = 5;       % frames read before the timed ones
            nbTimed = 30;       % frames timed; the median is reported

            % Receive data without junk in the upper four bits (what a device delivers
            % for 12-bit samples that it sign-extends), so raw and converted words
            % agree. The file is exactly one frame long (1048576 bytes): a frame must
            % not be longer than the file, and every frame is then the same.
            k = 0:N - 1;
            iq = [int16(mod(k, 4096) - 2048); int16(2047 - mod(k, 4096))];
            tc.Fx.writeWords('emu-rx_buf0.bin', typecast(iq(:).', 'uint16'));
            expected = complex(iq(1, :), iq(2, :)).';

            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            ids = ["voltage0_i", "voltage0_q"];

            % (a) the fast path: int16, complex
            r = iio.RxStream(rx, ids, SamplesPerFrame = N, OutputDataType = "int16", ...
                Complex = true);
            [msFast, a1, okA] = frameTime(@readFrame, r, nbWarmUp, nbTimed);
            r.close();

            % (b) the same frame from the storage words, paired in M code, and
            % (c, M-code form) that frame scaled to full scale in M code
            r = iio.RxStream(rx, ids, SamplesPerFrame = N, OutputDataType = "int16", ...
                Complex = false, Raw = true);
            [msMCode, b1, okB] = frameTime(@readAndPair, r, nbWarmUp, nbTimed);
            [msMCodeDouble, cm1, okCm] = frameTime(@readPairAndScale, r, nbWarmUp, nbTimed);
            r.close();

            % (c) the fast path: double, complex, full scale
            r = iio.RxStream(rx, ids, SamplesPerFrame = N, OutputDataType = "double", ...
                Complex = true, Scale = "fullscale");
            [msFastDouble, c1, okC] = frameTime(@readFrame, r, nbWarmUp, nbTimed);
            r.close();

            % the only assertions: the paths that are compared deliver the same frame
            tc.verifyEqual([okA okB okCm okC], [true true true true], 'every frame is valid');
            tc.verifyEqual(a1, expected);
            tc.verifyEqual(b1, a1);
            tc.verifyEqual(c1, cm1);
            tc.verifyEqual(c1(1:2), complex([-1; -0.99951171875], [0.99951171875; 0.9990234375]));

            % the report
            names = {'fast_int16_complex', 'mcode_int16_complex', ...
                'fast_double_fullscale', 'mcode_double_fullscale'};
            ms = [msFast, msMCode, msFastDouble, msMCodeDouble];
            txt = sprintf(['# iio.RxStream on emu, MATLAB R%s: %d samples per frame, 2 channels of 16 bit; ' ...
                'median of %d frames; <path> <ms per frame> <MS/s>\n'], version('-release'), N, nbTimed);
            for p = 1:numel(names)
                txt = [txt sprintf('%s %.3f %.1f\n', names{p}, ms(p), N / ms(p) / 1e3)]; %#ok<AGROW>
            end
            txt = [txt sprintf('ratio mcode_int16_complex/fast_int16_complex %.2f\n', msMCode / msFast)];
            txt = [txt sprintf('ratio mcode_double_fullscale/fast_double_fullscale %.2f\n', ...
                msMCodeDouble / msFastDouble)];
            tc.log(1, txt);

            folder = getenv('IIO_TEST_RESULTS');
            if isempty(folder)
                folder = tempdir;
            end
            if ~isfolder(folder)
                [~, ~] = mkdir(folder);     % with outputs: no error here, fopen reports it
            end
            name = fullfile(folder, 'timing.txt');
            fid = fopen(name, 'w');
            tc.assertGreaterThanOrEqual(fid, 3, ['cannot write ' name]);
            n = fwrite(fid, txt, 'char');
            fclose(fid);
            tc.verifyEqual(n, numel(txt), ['short write to ' name]);
        end
    end
end

function [ms, first, ok] = frameTime(step, r, nbWarmUp, nbTimed)
%FRAMETIME Median time of one call of STEP(R) in ms, after NBWARMUP calls that are not timed.
%   FIRST is the result of the first call, OK whether every frame was valid.
[first, ok] = step(r);
for f = 2:nbWarmUp
    [~, v] = step(r);
    ok = ok && v;
end
t = zeros(1, nbTimed);
for f = 1:nbTimed
    t0 = tic;
    [out, v] = step(r); %#ok<ASGLU> the frame is kept until the clock is read
    t(f) = toc(t0);
    ok = ok && v;
end
ms = 1e3 * median(t);
end

function [out, valid] = readFrame(r)
[out, valid] = r.read();
end

function [out, valid] = readAndPair(r)
[d, valid] = r.read();              % N-by-2, the storage words
rows = d.';                         % 2-by-N: one row per channel
out = complex(rows(1, :), rows(2, :)).';
end

function [out, valid] = readPairAndScale(r)
[out, valid] = readAndPair(r);
out = double(out) / 2048;
end
