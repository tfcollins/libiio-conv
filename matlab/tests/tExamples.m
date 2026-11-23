classdef tExamples < IioTestCase
    %TEXAMPLES The functions of matlab/examples, run on the emulated context.
    %   Each example returns its result, so the tests call them with the fixture's uri
    %   and device names in place of the ADALM-Pluto defaults. Receive data is the ramp
    %   of the fixture: sample k is (k - 2048) + (2047 - k)i (EmuFixture.rampWords).
    %   An example keeps its iio objects in local variables, so none is left when it
    %   returns or fails: the teardown of IioTestCase counts the handles.

    methods (TestClassSetup)
        function examplesOnPath(tc)
            % also when only the folder "matlab" itself is on the path
            here = fileparts(mfilename('fullpath'));
            tc.applyFixture(matlab.unittest.fixtures.PathFixture(fullfile(here, '..', 'examples')));
        end
    end

    methods (Test)
        function rxCapture(tc)
            % frames no longer than the data file of the fixture (4096 samples)
            data = rx_capture(Uri = tc.Fx.Uri, Device = "emu-rx", ...
                Channels = ["voltage0_i" "voltage0_q"], SamplesPerFrame = 1000, Frames = 3);
            tc.verifyClass(data, 'int16');
            tc.verifySize(data, [3000 1]);
            tc.verifyFalse(isreal(data));
            tc.verifyEqual(data(1), complex(int16(-2048), int16(2047)));
            tc.verifyEqual(data(3000), complex(int16(951), int16(-952)));
            k = (0:2999).';
            tc.verifyEqual(data, complex(int16(k - 2048), int16(2047 - k)));
        end

        function rxCaptureUnknownDevice(tc)
            % find_device finds nothing, and iio.RxStream refuses the empty device
            tc.verifyValidationError(@() rx_capture(Uri = tc.Fx.Uri, Device = "nope", ...
                Channels = ["voltage0_i" "voltage0_q"], SamplesPerFrame = 1000, Frames = 1));
        end

        function txCyclic(tc)
            frame = tx_cyclic(Uri = tc.Fx.Uri, Device = "emu-tx", ...
                Channels = ["voltage0_i" "voltage0_q"], Seconds = 0);
            tc.verifyClass(frame, 'double');
            tc.verifySize(frame, [1024 1]);
            tc.verifyFalse(isreal(frame));
            k = (0:1023).';
            want = 0.5 * exp(2i * pi * 8 * k / 1024);
            tc.verifyLessThan(max(abs(frame - want)), 1e-12);
            tc.verifyEqual(real(frame(1)), 0.5);
            % the options that shape the frame
            frame = tx_cyclic(Uri = tc.Fx.Uri, Device = "emu-tx", ...
                Channels = ["voltage0_i" "voltage0_q"], SamplesPerFrame = 64, Cycles = 1, ...
                Amplitude = 0.25, Seconds = 0);
            tc.verifySize(frame, [64 1]);
            tc.verifyLessThan(max(abs(frame - 0.25 * exp(2i * pi * (0:63).' / 64))), 1e-12);
        end

        function attributesTable(tc)
            t = attributes(Uri = tc.Fx.Uri, Phy = "emu-phy");
            tc.verifyClass(t, 'table');
            tc.verifySize(t, [14 1]);
            tc.verifyEqual(t.Properties.VariableNames, {'value'});
            tc.verifyEqual(reshape(t.Properties.RowNames, 1, []), {'calib_mode', ...
                'calib_mode_available', 'dbl', 'empty', 'ensm_mode', 'ensm_mode_available', ...
                'flag', 'frac', 'frac_available', 'gain', 'gain_available', 'hex', 'novalue', 'text'});
            tc.verifyClass(t.value, 'string');
            tc.verifyEqual(t{'ensm_mode', 'value'}, "fdd");
            tc.verifyEqual(t{'text', 'value'}, "hello world");
            tc.verifyEqual(t{'empty', 'value'}, "");
            tc.verifyTrue(ismissing(t{'novalue', 'value'}));    % reading it is iio:ENOENT
            tc.verifyEqual(nnz(ismissing(t.value)), 1);
        end

        function attributesWrite(tc)
            t = attributes(Uri = tc.Fx.Uri, Phy = "emu-phy", Set = "text", Value = "abc");
            tc.verifyEqual(t{'text', 'value'}, "abc");
            tc.verifyEqual(t{'ensm_mode', 'value'}, "fdd");
            t = attributes(Uri = tc.Fx.Uri, Phy = "emu-phy", Set = "text", Value = 2400000000);
            tc.verifyEqual(t{'text', 'value'}, "2400000000");
            t = attributes(Uri = tc.Fx.Uri, Phy = "emu-phy", Set = "text", Value = true);
            tc.verifyEqual(t{'text', 'value'}, "1");
        end

        function attributesUnknownNames(tc)
            tc.verifyError(@() attributes(Uri = tc.Fx.Uri, Phy = "nope"), 'attributes:noDevice');
            % find_attr finds nothing, and write refuses the empty attribute
            tc.verifyValidationError(@() attributes(Uri = tc.Fx.Uri, Phy = "emu-phy", ...
                Set = "nope", Value = 1));
            % Set without Value: refused before the context is opened
            tc.verifyError(@() attributes(Uri = tc.Fx.Uri, Phy = "emu-phy", Set = "text"), ...
                'attributes:noValue');
        end

        function lowLevelBlocks(tc)
            [raw, data] = low_level_blocks(Uri = tc.Fx.Uri, Device = "emu-rx", ...
                Channels = ["voltage0_i" "voltage0_q"], Samples = 1000);
            tc.verifyClass(raw, 'uint8');
            tc.verifySize(raw, [4000 1]);
            tc.verifyEqual(raw(1:4), uint8([0; 8; 255; 7]));
            % the block is the start of the data file, word for word (little-endian)
            tc.verifyEqual(reshape(typecast(raw, 'uint16'), 1, []), EmuFixture.rampWords(1000));
            tc.verifyClass(data, 'int16');
            tc.verifySize(data, [1000 1]);
            tc.verifyFalse(isreal(data));
            k = (0:999).';
            tc.verifyEqual(data, complex(int16(k - 2048), int16(2047 - k)));
        end

        function lowLevelBlocksUnknownChannel(tc)
            % enable refuses the empty result of find_channel; the mask and the context go
            tc.verifyError(@() low_level_blocks(Uri = tc.Fx.Uri, Device = "emu-rx", ...
                Channels = ["voltage0_i" "nope"], Samples = 1000), 'iio:EINVAL');
        end

        function listContexts(tc)
            % the emu backend cannot scan: no row
            t = list_contexts(Backends = "emu");
            tc.verifyClass(t, 'table');
            tc.verifySize(t, [0 2]);
            tc.verifyEqual(t.Properties.VariableNames, {'uri', 'description'});
            tc.verifyClass(t.uri, 'string');
            tc.verifyClass(t.description, 'string');
            tc.verifySize(t.uri, [0 1]);
        end

        function listContextsDefault(tc)
            % the local backend: how many rows depends on the machine, the form does not
            t = list_contexts();
            tc.verifyClass(t, 'table');
            tc.verifySize(t, [size(t, 1) 2]);
            tc.verifyEqual(t.Properties.VariableNames, {'uri', 'description'});
            tc.verifyClass(t.uri, 'string');
            tc.verifyClass(t.description, 'string');
            tc.verifyTrue(all(startsWith(t.uri, "local:")));
            % Backends = "" is no list: libiio's default list, which is the local backend
            % in the libiio of deps/build-libiio.sh
            tc.verifyEqual(list_contexts(Backends = ""), t);
        end
    end
end
