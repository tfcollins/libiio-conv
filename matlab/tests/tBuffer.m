classdef tBuffer < IioTestCase
    %TBUFFER iio.Buffer and iio.BufferStream: identity, attributes, scan elements, open and
    %   close, start, stop and cancel, the read-only mask of a stream, lifetime.

    methods (Test)
        function info(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            b = rx.buffers;
            tc.verifyClass(b, 'iio.Buffer');
            tc.verifySize(b, [1 1]);
            tc.verifyClass(b.output, 'logical');
            tc.verifyEqual(b.output, false);
            tc.verifyClass(b.device, 'iio.Device');
            tc.verifyTrue(b.device == rx);
            t = ctx.find_device("emu-tx").buffers;
            tc.verifySize(t, [1 1]);
            tc.verifyEqual(t.output, true);
            mb = ctx.find_device("emu-multi").buffers;
            tc.verifySize(mb, [1 2]);
            tc.verifyEqual([mb.output], [false true]);
            tc.verifyTrue(mb(1) ~= mb(2));
            tc.verifyTrue(mb(2).device == ctx.find_device("emu-multi"));
        end

        function attrs(tc)
            ctx = iio.Context(tc.Fx.Uri);
            buf = ctx.find_device("emu-rx").buffers;
            a = buf.attrs;
            tc.verifyClass(a, 'iio.Attr');
            tc.verifySize(a, [1 3]);
            tc.verifyEqual(string({a.name}), ["data_available" "length_align_bytes" "watermark"]);
            w = buf.find_attr("watermark");
            tc.verifyClass(w, 'iio.Attr');
            tc.verifySize(w, [1 1]);
            tc.verifyTrue(w == a(3));
            tc.verifyEqual(buf.find_attr("watermark").read(), '1024');
            tc.verifyEqual(buf.find_attr('watermark').name, 'watermark');
            none = buf.find_attr("nope");
            tc.verifyClass(none, 'iio.Attr');
            tc.verifySize(none, [1 0]);
            % a legacy buffer (no <buffer> element in the XML) has its attributes too
            t = ctx.find_device("emu-tx").buffers;
            ta = t.attrs;
            tc.verifyEqual(string({ta.name}), ["data_available" "watermark"]);
            tc.verifyTrue(isempty(t.find_attr("length_align_bytes")));
        end

        function scanElements(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            buf = rx.buffers;
            se = buf.scan_elements;
            tc.verifyClass(se, 'iio.Channel');
            tc.verifySize(se, [1 5]);
            tc.verifyTrue(all(se == rx.channels));
            % emu-tx: four scan elements, and one channel that is none
            tx = ctx.find_device("emu-tx");
            tc.verifySize(tx.buffers.scan_elements, [1 4]);
            tc.verifySize(tx.channels, [1 5]);
        end

        function openClose(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            v0q = rx.find_channel("voltage0_q");
            buf = rx.buffers;
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            v0q.enable(m);
            bs = buf.open(m);
            tc.verifyClass(bs, 'iio.BufferStream');
            tc.verifySize(bs, [1 1]);
            tc.verifyTrue(bs.buffer == buf);
            tc.verifyClass(bs.sample_size, 'double');
            tc.verifyEqual(bs.sample_size, 4);
            sm = bs.mask;
            tc.verifyClass(sm, 'iio.ChannelsMask');
            tc.verifyTrue(sm ~= m);
            ch = bs.mask.channels;
            tc.verifySize(ch, [1 2]);
            tc.verifyTrue(all(bs.mask.channels == [v0i v0q]));
            tc.verifyClass(bs.started, 'logical');
            tc.verifyEqual(bs.started, false);
            bs.close();
            bs.close();
            tc.verifyError(@() bs.started, 'iio:EBADF');
            tc.verifyError(@() bs.start(), 'iio:EBADF');
            tc.verifyError(@() bs.mask, 'iio:EBADF');
            % the buffer can be opened again, also with the constructor
            bs2 = iio.BufferStream(buf, m);
            tc.verifyEqual(bs2.started, false);
            tc.verifyTrue(bs2 ~= bs);
        end

        function maskMayGoAfterOpen(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v2 = rx.find_channel("voltage2");
            m = iio.ChannelsMask(rx);
            v2.enable(m);
            bs = rx.buffers.open(m);
            delete(m);
            tc.verifyEqual(bs.sample_size, 2);
            ch = bs.mask.channels;
            tc.verifySize(ch, [1 1]);
            tc.verifyTrue(ch == v2);
        end

        function startNeedsBlock(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            bs = rx.buffers.open(m);
            tc.verifyError(@() bs.start(), 'iio:EINVAL');
            tc.verifyEqual(bs.started, false);
        end

        function startStop(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            v0q = rx.find_channel("voltage0_q");
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            v0q.enable(m);
            bs = rx.buffers.open(m);
            blk = bs.create_block(32);
            tc.verifyClass(blk, 'iio.Block');
            tc.verifySize(blk, [1 1]);
            bs.start();
            tc.verifyEqual(bs.started, true);
            bs.start();     % again: nothing happens
            tc.verifyEqual(bs.started, true);
            bs.stop();
            tc.verifyEqual(bs.started, false);
            bs.stop();      % again: nothing happens
            tc.verifyEqual(bs.started, false);
            % a block smaller than one sample
            tc.verifyError(@() bs.create_block(2), 'iio:EINVAL');
        end

        function cancel(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            bs = rx.buffers.open(m);
            blk = bs.create_block(32);
            bs.start();
            bs.cancel();
            tc.verifyEqual(bs.started, true);
            tc.verifyClass(blk, 'iio.Block');
        end

        function emptyMask(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            buf = rx.buffers;
            m = iio.ChannelsMask(rx);
            tc.verifyError(@() buf.open(m), 'iio:EINVAL');
            tc.verifyError(@() iio.BufferStream(buf, m), 'iio:EINVAL');
        end

        function maskOfOtherDevice(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            tx = ctx.find_device("emu-tx");
            buf = rx.buffers;
            t0 = tx.find_channel("voltage0_i", true);
            m = iio.ChannelsMask(tx);
            t0.enable(m);
            tc.verifyError(@() buf.open(m), 'iio:EINVAL');
        end

        function maskIsReadOnly(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            v0q = rx.find_channel("voltage0_q");
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            bs = rx.buffers.open(m);
            tc.verifyError(@() v0i.disable(bs.mask), 'iio:EPERM');
            tc.verifyError(@() v0q.enable(bs.mask), 'iio:EPERM');
            ch = bs.mask.channels;
            tc.verifySize(ch, [1 1]);
            tc.verifyTrue(ch == v0i);
        end

        function closeInvalidatesBlocks(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            bs = rx.buffers.open(m);
            blk = bs.create_block(32);
            tc.verifyEqual(blk.nbytes, 32);
            bs.close();
            tc.verifyError(@() blk.nbytes, 'iio:EBADF');
            tc.verifyError(@() bs.create_block(32), 'iio:EBADF');
        end

        function contextDeleteWithOpenStream(tc)
            % every object is deleted in the order it was created, the context first
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            buf = rx.buffers;
            m = iio.ChannelsMask(rx);
            v0i.enable(m);
            bs = buf.open(m);
            blk = bs.create_block(32);
            bs.start();
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
            delete(rx);
            delete(v0i);
            delete(buf);
            delete(m);
            delete(bs);
            delete(blk);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end
    end
end
