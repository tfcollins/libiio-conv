classdef tStream < IioTestCase
    %TSTREAM iio.Stream: the ring of blocks on a buffer, block ownership, transmit, lifetime.

    methods
        function [buf, mask] = openBuffer(tc, device, is_output)
            %OPENBUFFER Buffer 0 of DEVICE and a mask with voltage0_i and voltage0_q enabled.
            %   The context stays alive through the parents of buf.
            ctx = iio.Context(tc.Fx.Uri);
            dev = ctx.find_device(device);
            bufs = dev.buffers;
            buf = bufs(1);
            mask = iio.ChannelsMask(dev);
            enable(dev.find_channel("voltage0_i", is_output), mask);
            enable(dev.find_channel("voltage0_q", is_output), mask);
        end
    end

    methods (Test)
        function rxBlocks(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            tc.verifyClass(st, 'iio.Stream');
            first = iio.Block.empty(1, 0);
            for n = 1:4
                blk = st.next();
                tc.verifyClass(blk, 'iio.Block');
                tc.verifySize(blk, [1 1]);
                tc.verifyEqual(blk.nbytes, 64);
                data = blk.read(OutputDataType = "int16", Complex = true);
                k = (16 * (n - 1):16 * n - 1).';
                tc.verifyClass(data, 'int16');
                tc.verifySize(data, [16 1]);
                tc.verifyFalse(isreal(data));
                tc.verifyEqual(data, complex(int16(k - 2048), int16(2047 - k)));
                if n == 1
                    first = blk;
                elseif n < 4
                    tc.verifyTrue(blk ~= first);
                end
            end
            % three blocks in rotation: the 4th is the 1st again
            tc.verifyTrue(blk == first);
        end

        function construction(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            tc.verifyEqual(st.samples_count, 16);
            tc.verifyEqual(st.nb_blocks, 3);
            tc.verifyClass(st.buffer, 'iio.Buffer');
            tc.verifyTrue(st.buffer == buf);
            % four blocks unless told otherwise
            delete(st);
            st = iio.Stream(buf, mask, 8);
            tc.verifyEqual(st.samples_count, 8);
            tc.verifyEqual(st.nb_blocks, 4);
            tc.verifyEqual(st.next().nbytes, 32);
        end

        function viaBuffer(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = buf.create_stream(mask, 16);
            tc.verifyClass(st, 'iio.Stream');
            tc.verifyEqual(st.nb_blocks, 4);
            tc.verifyEqual(st.samples_count, 16);
            tc.verifyTrue(st.buffer == buf);
            tc.verifyEqual(st.next().nbytes, 64);
        end

        function maskIsCopied(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 4, 2);
            delete(mask);
            data = st.next().read(OutputDataType = "int16", Complex = true);
            tc.verifyEqual(data, complex(int16((-2048:-2045).'), int16((2047:-1:2044).')));
        end

        function staleBlock(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            b1 = st.next();
            b2 = st.next();
            % the first block went back to the stream with the second next()
            tc.verifyError(@() b1.read(), 'iio:EPERM');
            tc.verifySize(b2.read(), [16 2]);
        end

        function blockIsBorrowed(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            blk = st.next();
            tc.verifyError(@() blk.enqueue(), 'iio:EPERM');
            blk.destroy();      % the block belongs to the stream: nothing happens
            tc.verifyEqual(blk.nbytes, 64);
            tc.verifyTrue(blk.parent == st);
        end

        function bufferStreamOfBlock(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            blk = st.next();
            bs = blk.buffer_stream;
            tc.verifyClass(bs, 'iio.BufferStream');
            tc.verifyEqual(bs.sample_size, 4);
            tc.verifyEqual(bs.started, true);       % the stream started it with the first next()
            tc.verifyError(@() bs.start(), 'iio:EPERM');
        end

        function tx(tc)
            [buf, mask] = tc.openBuffer("emu-tx", true);
            st = iio.Stream(buf, mask, 2, 1);
            blk = st.next();
            tc.verifyEqual(blk.nbytes, 8);
            n = blk.write(complex(int16([11; 13]), int16([12; 14])));
            tc.verifyEqual(n, 2);
            % the block is sent by the following next()
            blk = st.next();
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([11; 12; 13; 14]));
            n = blk.write(complex(int16([21; 23]), int16([22; 24])));
            tc.verifyEqual(n, 2);
            blk = st.next();
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([21; 22; 23; 24]));
            % the block filled last is never sent
            blk.write(complex(int16([31; 33]), int16([32; 34])));
            st.destroy();
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([21; 22; 23; 24]));
        end

        function badArguments(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            [txbuf, txmask] = tc.openBuffer("emu-tx", true);
            tc.verifyError(@() iio.Stream(buf, mask, 16, 0), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(buf, mask, 0, 3), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(buf, mask, 16, 2.5), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(buf, mask, 16, NaN), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(buf, mask), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(buf, mask, [16 16]), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(mask, mask, 16), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(buf, buf, 16), 'iio:EINVAL');
            % a mask of another device (another context here)
            tc.verifyError(@() iio.Stream(buf, txmask, 16), 'iio:EINVAL');
            tc.verifyError(@() iio.Stream(txbuf, mask, 16), 'iio:EINVAL');
            % nothing is left of the failed attempts: the buffer still opens
            st = iio.Stream(buf, mask, 16, 1);
            tc.verifyEqual(st.next().nbytes, 64);
        end

        function emptyForms(tc)
            s = iio.Stream();
            tc.verifyEqual(s.samples_count, 0);
            tc.verifyEqual(s.nb_blocks, 0);
            tc.verifyEmpty(s.buffer);
            e = iio.Stream.empty(1, 0);
            tc.verifySize(e, [1 0]);
            tc.verifyClass(e, 'iio.Stream');
            delete(s);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function cancel(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            st.next();
            st.next();
            st.cancel();
            st.destroy();
            tc.verifyError(@() st.next(), 'iio:EBADF');
            tc.verifyError(@() st.cancel(), 'iio:EBADF');
        end

        function destroyTwice(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 3);
            blk = st.next();
            st.destroy();
            st.destroy();
            tc.verifyTrue(isvalid(st));
            % the blocks went with the stream
            tc.verifyError(@() blk.nbytes, 'iio:EBADF');
            delete(st);
            tc.verifyFalse(isvalid(st));
            % the buffer is free again
            st = iio.Stream(buf, mask, 16, 3);
            tc.verifyEqual(st.next().nbytes, 64);
        end

        function contextGoesFirst(tc)
            ctx = iio.Context(tc.Fx.Uri);
            dev = ctx.find_device("emu-rx");
            bufs = dev.buffers;
            mask = iio.ChannelsMask(dev);
            enable(dev.find_channel("voltage0_i"), mask);
            st = iio.Stream(bufs(1), mask, 16, 2);
            blk = st.next();
            tc.verifyEqual(blk.nbytes, 32);
            delete(ctx);
            tc.verifyError(@() st.next(), 'iio:EBADF');
            tc.verifyError(@() blk.nbytes, 'iio:EBADF');
            st.destroy();       % stale: nothing happens
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function keepsContextAlive(tc)
            [buf, mask] = tc.openBuffer("emu-rx", false);
            st = iio.Stream(buf, mask, 16, 2);
            clear buf mask
            blk = st.next();
            clear st
            % the block holds the stream, the stream its buffer, and so on up to the context
            tc.verifyEqual(blk.nbytes, 64);
            clear blk
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end
    end
end
