classdef tRxStream < IioTestCase
    %TRXSTREAM iio.RxStream: frames from emu-rx in every output form, channel order, gains,
    %   flush, close and lifetime; "no frame in time" on the fake backend.
    %   The receive data is the ramp of EmuFixture: sample k is (k - 2048) + (2047 - k)i
    %   in the low 12 bits of its two words.

    methods (Test)
        function parityConfig(tc)
            % what a toolbox receiver delivers: int16, complex, the storage words as they are
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                OutputDataType = "int16", Complex = true, Raw = true);
            [d, v] = r.read();
            tc.verifyClass(v, 'logical');
            tc.verifyEqual(v, true);
            tc.verifySize(d, [1024 1]);
            tc.verifyClass(d, 'int16');
            tc.verifyFalse(isreal(d));
            w = tc.Fx.readInt16('emu-rx_buf0.bin');     % the words of the file, as int16
            tc.verifyEqual(d, complex(w(1:2:2048), w(2:2:2048)));
            tc.verifyEqual(d(1:2), complex(int16([2048; 6145]), int16([2047; 2046])));
        end

        function converted(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                OutputDataType = "int16", Complex = true);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            k = (0:1023).';
            tc.verifyEqual(d, complex(int16(k - 2048), int16(2047 - k)));
        end

        function consecutiveFrames(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                OutputDataType = "int16", Complex = true);
            [d1, v1] = r.read();
            [d2, v2] = r.read();
            tc.verifyEqual([v1 v2], [true true]);
            k = (0:1023).';
            tc.verifyEqual(d1, complex(int16(k - 2048), int16(2047 - k)));
            k = (1024:2047).';
            tc.verifyEqual(d2, complex(int16(k - 2048), int16(2047 - k)));
        end

        function defaults(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyClass(d, 'double');
            tc.verifyTrue(isreal(d));
            tc.verifySize(d, [8 2]);
            tc.verifyEqual(d, [(-2048:-2041).', (2047:-1:2040).']);
            % the read-only copies of the options
            tc.verifyTrue(r.device == rx);
            tc.verifyEqual(r.channels, {'voltage0_i', 'voltage0_q'});
            tc.verifyEqual(r.SamplesPerFrame, 8);
            tc.verifyEqual(r.OutputDataType, 'double');
            tc.verifyEqual(r.Complex, false);
            tc.verifyEqual(r.Scale, 'none');
            tc.verifySize(r.Gain, [1 0]);
            tc.verifySize(r.Offset, [1 0]);
            tc.verifyEqual(r.Raw, false);
            tc.verifyEqual(r.NumBlocks, 2);
            tc.verifyEqual(r.Timeout, -1);
        end

        function singleFullscale(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                OutputDataType = "single", Scale = "fullscale", Complex = true);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyClass(d, 'single');
            tc.verifySize(d, [1024 1]);
            tc.verifyTrue(d(1) == single(-1) + single(0.99951171875) * 1i);
            tc.verifyEqual(d(2), complex(single(-0.99951171875), single(0.9990234375)));
            tc.verifyEqual(r.OutputDataType, 'single');
            tc.verifyEqual(r.Scale, 'fullscale');
        end

        function twoComplexChannelsOrder(tc)
            % column k is pair k of the list, whatever the order of the channels on the device
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage1_i", "voltage1_q", "voltage0_i", "voltage0_q"], ...
                SamplesPerFrame = 512, OutputDataType = "int16", Complex = true);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifySize(d, [512 2]);
            tc.verifyEqual(d(1, 1), complex(int16(-2047), int16(2046)));
            tc.verifyEqual(d(1, 2), complex(int16(-2048), int16(2047)));
            tc.verifyEqual(d(512, 1), complex(int16(-1025), int16(1024)));
            tc.verifyEqual(d, swappedFrame());
        end

        function channelObjects(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            chans = [rx.find_channel("voltage1_i"), rx.find_channel("voltage1_q"), ...
                rx.find_channel("voltage0_i"), rx.find_channel("voltage0_q")];
            r = iio.RxStream(rx, chans, SamplesPerFrame = 512, OutputDataType = "int16", Complex = true);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d, swappedFrame());
            tc.verifyEqual(r.channels, {'voltage1_i', 'voltage1_q', 'voltage0_i', 'voltage0_q'});
        end

        function charAndCellInput(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            % one channel: bits 15..2 of every word
            want = double(EmuFixture.s14shift2(EmuFixture.rampWords(4))).';
            r = iio.RxStream(rx, "voltage2", SamplesPerFrame = 8);
            a = r.read();
            tc.verifyEqual(r.channels, {'voltage2'});
            r.close();
            r = iio.RxStream(rx, 'voltage2', SamplesPerFrame = 8);
            b = r.read();
            tc.verifyEqual(r.channels, {'voltage2'});
            r.close();
            tc.verifySize(a, [8 1]);
            tc.verifyEqual(a(1:4), [512; 511; 1536; 511]);
            tc.verifyEqual(a, want);
            tc.verifyEqual(b, a);
            % a cell array of character vectors
            r = iio.RxStream(rx, {'voltage0_i', 'voltage0_q'}, SamplesPerFrame = 8);
            c = r.read();
            tc.verifyClass(r.channels, 'cell');
            tc.verifySize(r.channels, [1 2]);
            tc.verifyClass(r.channels{1}, 'char');
            tc.verifyEqual(r.channels, {'voltage0_i', 'voltage0_q'});
            r.close();
            % a column of strings is the same list
            r = iio.RxStream(rx, ["voltage0_i"; "voltage0_q"], SamplesPerFrame = 8);
            e = r.read();
            tc.verifySize(r.channels, [1 2]);
            tc.verifyEqual(c, [(-2048:-2041).', (2047:-1:2040).']);
            tc.verifyEqual(e, c);
        end

        function gainPerColumn(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                Complex = true, Gain = 2);
            d = r.read();
            tc.verifyClass(d, 'double');
            tc.verifyEqual(d(1), complex(-4096, 4094));
            tc.verifyEqual(r.Gain, 2);
            r.close();
            swapped = ["voltage1_i", "voltage1_q", "voltage0_i", "voltage0_q"];
            r = iio.RxStream(rx, swapped, SamplesPerFrame = 512, Complex = true, Gain = [2 3]);
            d = r.read();
            tc.verifyEqual(d(1, :), [complex(-4094, 4092), complex(-6144, 6141)]);
            tc.verifyEqual(r.Gain, [2 3]);
            r.close();
            tc.verifyError(@() iio.RxStream(rx, swapped, SamplesPerFrame = 512, Complex = true, ...
                Gain = [1 2 3]), 'iio:EINVAL');
        end

        function flush(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                OutputDataType = "int16", Complex = true, NumBlocks = 2);
            d = r.read();
            tc.verifyEqual(d(1), complex(int16(-2048), int16(2047)));
            tf = r.flush();
            tc.verifyClass(tf, 'logical');
            tc.verifyEqual(tf, true);
            % the two frames captured meanwhile were dropped
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d(1), complex(int16(1024), int16(-1025)));
        end

        function closeTwiceAndDelete(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8);
            n = iio.internal.handle_count();
            r.close();
            tc.verifyEqual(iio.internal.handle_count(), n - 1);    % the stream is gone
            r.close();
            delete(r);
            tc.verifyFalse(isvalid(r));
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
            % a stream that is only deleted is closed too
            r2 = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8);
            tc.verifyEqual(iio.internal.handle_count(), n);
            delete(r2);
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
        end

        function scaleWithIntegerClass(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            n = iio.internal.handle_count();
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8, ...
                OutputDataType = "int16", Scale = "fullscale"), 'iio:EINVAL');
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8, ...
                OutputDataType = "int16", Gain = 2), 'iio:EINVAL');
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8, ...
                Scale = "iio", Raw = true), 'iio:EINVAL');
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i", "voltage0_q", "voltage1_i"], ...
                SamplesPerFrame = 8, Complex = true), 'iio:EINVAL');
            % a failed open leaves at most the buffer of the device registered, never a stream
            tc.verifyLessThanOrEqual(iio.internal.handle_count(), n + 1);
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8);
            tc.verifyEqual(r.read(), [(-2048:-2041).', (2047:-1:2040).']);
        end

        function unknownChannel(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i", "voltage9"], SamplesPerFrame = 8), 'iio:ENOENT');
            % an output channel id on a receive stream is unknown as well
            tx = ctx.find_device("emu-tx");
            tc.verifyError(@() iio.RxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8), 'iio:ENOENT');
        end

        function badOptionValue(tc)
            % rejected by the arguments block, before the gateway is called
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            n = iio.internal.handle_count();
            id = '';
            try
                iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8, OutputDataType = "int12");
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
            id = '';
            try
                iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 0);
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
            id = '';
            try
                iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8, Timeout = -2);
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
            tc.verifyEqual(iio.internal.handle_count(), n);
        end

        function timeoutGivesInvalid(tc)
            % the fake backend delivers three frames, then nothing
            ctx = tc.Fx.fakeContext();
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                OutputDataType = "int16", Complex = true, Timeout = 0);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d, complex(int16([0; 2; 4; 6]), int16([1; 3; 5; 7])));
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d(1), complex(int16(256), int16(257)));
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d(1), complex(int16(512), int16(513)));
            [d, v] = r.read();
            tc.verifyClass(v, 'logical');
            tc.verifyEqual(v, false);
            tc.verifySize(d, [4 1]);
            tc.verifyClass(d, 'int16');
            tc.verifyEqual(real(d), zeros(4, 1, 'int16'));
            tc.verifyEqual(imag(d), zeros(4, 1, 'int16'));
            % and again: still no error
            [d, v] = r.read();
            tc.verifyEqual(v, false);
            tc.verifyEqual(real(d), zeros(4, 1, 'int16'));
            tc.verifyEqual(imag(d), zeros(4, 1, 'int16'));
            tc.verifyEqual(r.Timeout, 0);
        end

        function readAfterClose(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 8);
            r.close();
            tc.verifyError(@() r.read(), 'iio:EBADF');
            tc.verifyError(@() r.flush(), 'iio:EBADF');
        end

        function reopen(tc)
            % release, then another frame size on the same device: the first frame again
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1024, ...
                OutputDataType = "int16", Complex = true);
            d = r.read();
            tc.verifySize(d, [1024 1]);
            r.close();
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 64, ...
                OutputDataType = "int16", Complex = true);
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            k = (0:63).';
            tc.verifyEqual(d, complex(int16(k - 2048), int16(2047 - k)));
        end

        function contextSurvives(tc)
            % the stream holds the device, the device holds the context
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 16, ...
                OutputDataType = "int16", Complex = true);
            clear ctx rx
            [d, v] = r.read();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d(1), complex(int16(-2048), int16(2047)));
            tc.verifyEqual(r.device.name, 'emu-rx');
            clear r
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function contextDeletedFirst(tc)
            % the context goes while the stream exists: the stream is closed with it
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 16);
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyError(@() r.read(), 'iio:EBADF');
            r.close();      % nothing happens
            delete(r);
            tc.verifyFalse(isvalid(r));
        end
    end
end

function d = swappedFrame()
%SWAPPEDFRAME First 512-by-2 frame of the list voltage1, voltage0 (int16, complex).
%   With both pairs enabled sample s holds ramp sample 2s for voltage0 and 2s + 1 for voltage1.
s = (0:511).';
k1 = 2 * s + 1;
k0 = 2 * s;
d = [complex(int16(k1 - 2048), int16(2047 - k1)), complex(int16(k0 - 2048), int16(2047 - k0))];
end
