classdef tTxStream < IioTestCase
    %TTXSTREAM iio.TxStream: frames to emu-tx from every input form, rounding and saturation,
    %   partial frames, the last frame at close, cyclic transmit, argument errors; "no free
    %   block" on the fake backend.
    %   emu writes a transmit block to emu-tx_buf0.bin when the block comes back (int16,
    %   I then Q), so a test flushes or closes the stream before it reads the file.

    methods (Test)
        function complexInt16(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            v = t.write(frameA());
            tc.verifyClass(v, 'logical');
            tc.verifyEqual(v, true);
            tf = t.flush();
            tc.verifyClass(tf, 'logical');
            tc.verifyEqual(tf, true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
            % the read-only copies of the options
            tc.verifyTrue(t.device == tx);
            tc.verifyEqual(t.channels, {'voltage0_i', 'voltage0_q'});
            tc.verifyEqual(t.SamplesPerFrame, 4);
            tc.verifyEqual(t.Complex, true);
            tc.verifyEqual(t.Scale, 'none');
            tc.verifySize(t.Gain, [1 0]);
            tc.verifySize(t.Offset, [1 0]);
            tc.verifyEqual(t.Raw, false);
            tc.verifyEqual(t.NumBlocks, 1);
            tc.verifyEqual(t.Timeout, -1);
            tc.verifyEqual(t.Cyclic, false);
        end

        function realInputOnComplex(tc)
            % a real array on a complex stream sends Q = 0, whatever an earlier frame left there
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            want = int16([7; 0; -8; 0; 9; 0; -10; 0]);
            tc.verifyEqual(t.write(frameA()), true);
            tc.verifyEqual(t.write(complex(int16([7; -8; 9; -10]), int16(0))), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), want);
            tc.verifyEqual(t.write(frameA()), true);
            tc.verifyEqual(t.write(int16([7; -8; 9; -10])), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), want);
        end

        function doubleInputSaturates(tc)
            % halves round to the even integer; values beyond int16 saturate
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            d = [100.4 - 200.6i; 32767.9 - 40000i; 0.5 + 1.5i; 2.5 - 0.5i];
            tc.verifyEqual(t.write(d), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; -201; 32767; -32768; 0; 2; 2; 0]));
        end

        function int32InputSaturates(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            d = complex(int32([40000; 5; 32768; 0]), int32([-40000; -5; -32769; 1]));
            tc.verifyEqual(t.write(d), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([32767; -32768; 5; -5; 32767; -32768; 0; 1]));
        end

        function otherClasses(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            tc.verifyEqual(t.write(complex(single([1.5; 3; -5; 70000]), single([-1.5; 4; 6; -70000]))), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([2; -2; 3; 4; -5; 6; 32767; -32768]));
            tc.verifyEqual(t.write(complex(uint8([1; 3; 255; 7]), uint8([2; 4; 0; 8]))), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([1; 2; 3; 4; 255; 0; 7; 8]));
        end

        function partialFrame(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            tc.verifyEqual(t.write(complex(int16([5; 7]), int16([6; 8]))), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([5; 6; 7; 8]));    % 8 bytes
        end

        function lastFrameSentOnClose(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 2, Complex = true);
            tc.verifyEqual(t.write(frameA()), true);
            tc.verifyEqual(t.write(complex(int16([11; 13; 15; 17]), int16([12; 14; 16; 18]))), true);
            tc.verifyEqual(t.write(complex(int16([21; 23; 25; 27]), int16([22; 24; 26; 28]))), true);
            t.close();
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16((21:28).'));
            tc.verifyError(@() t.write(frameA()), 'iio:EBADF');
            tc.verifyError(@() t.flush(), 'iio:EBADF');
        end

        function wrongColumns(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            tc.verifyError(@() t.write(complex(zeros(4, 2, 'int16'), ones(4, 2, 'int16'))), 'iio:EINVAL');
            tc.verifyError(@() t.write(zeros(4, 2)), 'iio:EINVAL');
            tc.verifyError(@() t.write(complex(zeros(4, 1, 2, 'int16'), ones(4, 1, 2, 'int16'))), 'iio:EINVAL');
            % nothing was sent by the rejected calls
            tc.verifyEqual(t.write(frameA()), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
        end

        function tooManyRows(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            tc.verifyError(@() t.write(complex(zeros(5, 1, 'int16'), ones(5, 1, 'int16'))), 'iio:EINVAL');
            tc.verifyError(@() t.write(zeros(5, 1)), 'iio:EINVAL');
        end

        function emptyData(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            tc.verifyError(@() t.write(zeros(0, 1, 'int16')), 'iio:EINVAL');
            tc.verifyError(@() t.write([]), 'iio:EINVAL');
        end

        function charData(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true);
            tc.verifyError(@() t.write(('abcd').'), 'iio:EINVAL');
            tc.verifyError(@() t.write(true(4, 1)), 'iio:EINVAL');
            tc.verifyError(@() t.write({1; 2; 3; 4}), 'iio:EINVAL');
            tc.verifyError(@() t.write(sparse([1; 2; 3; 4])), 'iio:EINVAL');
            tc.verifyError(@() t.write("abcd"), 'iio:EINVAL');
        end

        function realStream(tc)
            % without Complex every channel is a column
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, NumBlocks = 1);
            tc.verifyEqual(t.write(int16([100 1; -200 2; 300 3; -400 4])), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
            tc.verifyEqual(t.Complex, false);
        end

        function complexDataOnRealStream(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, NumBlocks = 1);
            tc.verifyError(@() t.write(complex(zeros(4, 2, 'int16'), ones(4, 2, 'int16'))), 'iio:EINVAL');
        end

        function cyclicSecondWrite(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 4, Complex = true, Cyclic = true);
            tc.verifyEqual(t.Cyclic, true);
            tc.verifyEqual(t.write(frameA()), true);
            tc.verifyError(@() t.write(frameA()), 'iio:EBUSY');
            tc.verifyEqual(t.flush(), true);    % returns at once: a cyclic frame never completes
            t.close();
        end

        function fullscale(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true, Scale = "fullscale");
            tc.verifyEqual(t.write([1 - 1i; 0.5 - 0.5i; 0.25; 0]), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([32767; -32768; 16384; -16384; 8192; 0; 0; 0]));
            % an integer class cannot be scaled
            tc.verifyError(@() t.write(frameA()), 'iio:EINVAL');
            tc.verifyEqual(t.Scale, 'fullscale');
        end

        function gainAndOffset(tc)
            % the device gets (DATA - Offset) / Gain, per column: the inverse of iio.RxStream
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Gain = [2 4], Offset = [10 -20]);
            tc.verifyEqual(t.Gain, [2 4]);
            tc.verifyEqual(t.Offset, [10 -20]);
            tc.verifyEqual(t.write([100 100; 200 200; 300 300; 10 -20]), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([45; 30; 95; 55; 145; 80; 0; 0]));
            t.close();
            % complex: the offset is taken from the real and from the imaginary part
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true, Gain = 2, Offset = 10);
            tc.verifyEqual(t.write([100 + 50i; 10 + 20i; 10 + 10i; 12 + 8i]), true);
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([45; 20; 0; 5; 0; 0; 1; -1]));
        end

        function multiBufferDevice(tc)
            % emu-multi: the output buffer is the second one; 12 bits in the upper part of each word
            ctx = iio.Context(tc.Fx.Uri);
            multi = ctx.find_device("emu-multi");
            t = iio.TxStream(multi, ["voltage0", "voltage1"], SamplesPerFrame = 2, Scale = "fullscale");
            tc.verifyEqual(t.write([0.5 1; -0.5 -1]), true);
            tc.verifyEqual(t.flush(), true);
            w = typecast(tc.Fx.readInt16('emu-multi_buf1.bin'), 'uint16');
            tc.verifyEqual(w, uint16([16384; 32752; 49152; 32768]));    % 4000 7ff0 c000 8000
            t.close();
            t = iio.TxStream(multi, ["voltage0", "voltage1"], SamplesPerFrame = 2);
            tc.verifyEqual(t.write(int16([32767 -32768; 1 -1])), true);
            tc.verifyEqual(t.flush(), true);
            w = typecast(tc.Fx.readInt16('emu-multi_buf1.bin'), 'uint16');
            tc.verifyEqual(w, uint16([32752; 32768; 16; 65520]));       % 7ff0 8000 0010 fff0
            t.close();
            t = iio.TxStream(multi, ["voltage0", "voltage1"], SamplesPerFrame = 2, Raw = true, Buffer = 1);
            tc.verifyEqual(t.write(int16([32767 -32768; 1 -1])), true);
            tc.verifyEqual(t.flush(), true);
            w = typecast(tc.Fx.readInt16('emu-multi_buf1.bin'), 'uint16');
            tc.verifyEqual(w, uint16([32767; 32768; 1; 65535]));        % 7fff 8000 0001 ffff
            t.close();
            % the first buffer of the device is its input buffer
            tc.verifyError(@() iio.TxStream(multi, ["voltage0", "voltage1"], SamplesPerFrame = 2, Buffer = 0), ...
                'iio:EINVAL');
        end

        function openErrors(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            rx = ctx.find_device("emu-rx");
            tc.verifyError(@() iio.TxStream(tx, ["voltage0_i", "voltage9"], SamplesPerFrame = 4), 'iio:ENOENT');
            tc.verifyError(@() iio.TxStream(rx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4), 'iio:ENOENT');
            tc.verifyError(@() iio.TxStream(tx, ["voltage0_i", "voltage0_i"], SamplesPerFrame = 4), 'iio:EINVAL');
            % the first argument is a device: a context or a channel is not converted to one
            tc.verifyValidationError(@() iio.TxStream(ctx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4));
            tc.verifyValidationError(@() iio.TxStream(tx.find_channel("voltage0_i", true), "voltage0_q", ...
                SamplesPerFrame = 4));
            tc.verifyError(@() iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, Buffer = 3), ...
                'iio:EINVAL');
            id = '';
            try
                iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, Scale = "full");
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
        end

        function closeTwiceAndDelete(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, Complex = true);
            n = iio.internal.handle_count();
            tc.verifyEqual(t.write(frameA()), true);
            t.close();
            tc.verifyEqual(iio.internal.handle_count(), n - 1);    % the stream is gone
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
            t.close();
            delete(t);
            tc.verifyFalse(isvalid(t));
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
            % release and re-create with another frame size
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 2, Complex = true);
            tc.verifyEqual(iio.internal.handle_count(), n);
            tc.verifyEqual(t.write(complex(int16([5; 7]), int16([6; 8]))), true);
            clear t
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([5; 6; 7; 8]));
        end

        function noFreeBlockGivesInvalid(tc)
            % the fake backend completes two transmit blocks, then none
            ctx = tc.Fx.fakeContext();
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 2, Complex = true, Timeout = 0);
            for k = 1:4
                tc.verifyEqual(t.write(frameA()), true);
            end
            v = t.write(frameA());
            tc.verifyClass(v, 'logical');
            tc.verifyEqual(v, false);
            tc.verifyEqual(t.flush(), false);
        end

        function timeoutConstants(tc)
            % FromContext is the default: a frame to emu-tx is sent within the context timeout
            ctx = iio.Context(tc.Fx.Uri);
            t = iio.TxStream(ctx.find_device("emu-tx"), ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 1, Complex = true, Timeout = iio.StreamTimeout.FromContext);
            tc.verifyEqual(t.Timeout, -1);
            tc.verifyEqual(t.write(frameA()), true);
            tc.verifyEqual(t.flush(), true);
            t.close();
            % NonBlocking: the fake backend completes two transmit blocks, then none, and
            % write and flush return at once
            fake = tc.Fx.fakeContext();
            t = iio.TxStream(fake.find_device("emu-tx"), ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, ...
                NumBlocks = 2, Complex = true, Timeout = iio.StreamTimeout.NonBlocking);
            tc.verifyEqual(t.Timeout, 0);
            for k = 1:4
                tc.verifyEqual(t.write(frameA()), true);
            end
            tc.verifyEqual(t.write(frameA()), false);
            tc.verifyEqual(t.flush(), false);
        end

        function rxAndTxTogether(tc)
            ctx = iio.Context(tc.Fx.Uri);
            r = iio.RxStream(ctx.find_device("emu-rx"), ["voltage0_i", "voltage0_q"], ...
                SamplesPerFrame = 1024, OutputDataType = "int16", Complex = true);
            t = iio.TxStream(ctx.find_device("emu-tx"), ["voltage0_i", "voltage0_q"], ...
                SamplesPerFrame = 4, Complex = true);
            [d1, v1] = r.read();
            tc.verifyEqual(t.write(frameA()), true);
            [d2, v2] = r.read();
            tc.verifyEqual(t.flush(), true);
            tc.verifyEqual([v1 v2], [true true]);
            k = (0:1023).';
            tc.verifyEqual(d1, complex(int16(k - 2048), int16(2047 - k)));
            k = (1024:2047).';
            tc.verifyEqual(d2, complex(int16(k - 2048), int16(2047 - k)));
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
        end

        function contextDeletedFirst(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tx = ctx.find_device("emu-tx");
            t = iio.TxStream(tx, ["voltage0_i", "voltage0_q"], SamplesPerFrame = 4, Complex = true);
            tc.verifyEqual(t.write(frameA()), true);
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
            % the frame in flight went out before the buffer was closed
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
            tc.verifyError(@() t.write(frameA()), 'iio:EBADF');
            t.close();      % nothing happens
            delete(t);
            tc.verifyFalse(isvalid(t));
        end
    end
end

function a = frameA()
%FRAMEA The 4-by-1 complex int16 frame of most cases; sent as 100 1 -200 2 300 3 -400 4.
a = complex(int16([100; -200; 300; -400]), int16([1; 2; 3; 4]));
end
