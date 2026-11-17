classdef tFakeBackend < IioTestCase
    %TFAKEBACKEND The test-only fake backend, reached through the test hooks of the MEX file.
    %   What emu cannot show: the timeout arriving at the backend, a binary attribute
    %   write kept byte for byte, a stream whose mask is wider than the requested one,
    %   and the order of the backend calls when everything is shut down at once.
    %   There is no skip logic: a MEX file built without the test hooks fails these tests.

    methods (Test)
        function adoptFakeContext(tc)
            ctx = tc.Fx.fakeContext();
            tc.verifyClass(ctx, 'iio.Context');
            tc.verifyEqual(ctx.name, 'fake');
            tc.verifyEqual(ctx.description, 'Emulated fixture & test context (fake)');
            tc.verifyEqual(numel(ctx.devices), 8);
        end

        function timeoutReachesBackend(tc)
            ctx = tc.Fx.fakeContext();
            n = iio_mex('test_fake_last_timeout');
            tc.verifyClass(n, 'double');
            tc.verifyEqual(n, -12345);      % the backend's set_timeout was not called yet
            ctx.set_timeout(250);
            tc.verifyEqual(iio_mex('test_fake_last_timeout'), 250);
            ctx.set_timeout(Inf);
            tc.verifyEqual(iio_mex('test_fake_last_timeout'), -1);
        end

        function attributeReadThroughFake(tc)
            ctx = tc.Fx.fakeContext();
            text = ctx.find_device("emu-phy").find_attr("text");
            tc.verifyEqual(text.read(), 'fake:text');
        end

        function binaryAttributeWrite(tc)
            % 47000 bytes with zero bytes among them go by their byte count
            ctx = tc.Fx.fakeContext();
            none = iio_mex('test_fake_last_write');
            tc.verifyClass(none, 'uint8');
            tc.verifySize(none, [1 0]);
            p = uint8(mod(0:46999, 256));
            n = ctx.find_device("emu-phy").find_attr("text").write_raw(p);
            tc.verifyEqual(n, 47000);
            w = iio_mex('test_fake_last_write');
            tc.verifyClass(w, 'uint8');
            tc.verifySize(w, [1 47000]);
            tc.verifyEqual(w, p);
        end

        function coupledChannelWidensSample(tc)
            % the backend enables one more channel than the mask asks for, as hardware
            % with coupled channels does: sizes and data follow the stream's own mask
            ctx = tc.Fx.fakeContext();
            rx = ctx.find_device("emu-rx");
            iio_mex('test_fake_set', 'couple', 'voltage1_i');
            mask = iio.ChannelsMask(rx);
            mask.channels = [rx.find_channel("voltage0_i") rx.find_channel("voltage0_q")];
            tc.verifyEqual(mask.sample_size, 4);
            bufs = rx.buffers;
            bs = bufs(1).open(mask);
            tc.verifyEqual(bs.sample_size, 6);
            tc.verifyEqual(numel(bs.mask.channels), 3);
            tc.verifyEqual(mask.sample_size, 4);    % the mask of the caller is not the stream's
            bs.close();
            r = iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame = 4, ...
                OutputDataType = "int16", NumBlocks = 1, Timeout = 0);
            [d, v] = r.read();
            tc.verifyClass(v, 'logical');
            tc.verifyTrue(v);
            % three 16-bit words per sample (0, 1, 2; 3, 4, 5; ...): columns 1 and 2 of 3
            tc.verifyEqual(d, int16([0 1; 3 4; 6 7; 9 10]));
        end

        function closeOrderOnShutdown(tc)
            % shutdown with a started stream: cancel, stop, blocks, close, and only
            % then the context (no letter); nothing is left and nothing is destroyed twice
            ctx = tc.Fx.fakeContext();
            rx = ctx.find_device("emu-rx");
            tc.verifyEqual(iio_mex('test_fake_log'), '');
            r = iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 2, OutputDataType = "int16", Complex = true);
            tc.verifyEqual(iio_mex('test_fake_log'), 'OBBEES');
            iio.internal.shutdown();
            tc.verifyEqual(iio_mex('test_fake_log'), 'OBBEESXsFFXC');
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyError(@() r.read(), 'iio:EBADF');
            again = tc.Fx.fakeContext();
            tc.verifyEqual(again.name, 'fake');
            tc.verifyEqual(iio_mex('test_fake_log'), '');
            tc.verifyEqual(iio.internal.handle_count(), 1);
        end
    end
end
