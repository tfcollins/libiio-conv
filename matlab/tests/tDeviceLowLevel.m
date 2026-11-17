classdef tDeviceLowLevel < IioTestCase
    %TDEVICELOWLEVEL iio.Device: sample size of a mask, hardware registers, debug and event attributes, trigger.

    methods (Test)
        function sampleSize(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            ch = rx.channels;
            mask = iio.ChannelsMask(rx);
            n = rx.sample_size(mask);           % a new mask: nothing enabled
            tc.verifyClass(n, 'double');
            tc.verifyEqual(n, 0);
            tc.verifyEqual(mask.sample_size, 0);
            enable(ch(1:2), mask);              % voltage0_i, voltage0_q: 2 x 16 bit
            tc.verifyEqual(rx.sample_size(mask), 4);
            tc.verifyEqual(mask.sample_size, 4);
            buffers = rx.buffers;
            bs = buffers(1).open(mask);
            tc.verifyEqual(bs.sample_size, 4);
            all5 = iio.ChannelsMask(rx);
            enable(ch, all5);                   % all five channels
            tc.verifyEqual(rx.sample_size(all5), 10);
            tc.verifyEqual(all5.sample_size, 10);
            tc.verifyEqual(mask.sample_size, 4);
        end

        function sampleSizeOfAnotherDevice(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            tx = ctx.find_device("emu-tx");
            txMask = iio.ChannelsMask(tx);
            tc.verifyEqual(tx.sample_size(txMask), 0);
            tc.verifyError(@() rx.sample_size(txMask), 'iio:EINVAL');
        end

        function registers(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            tc.verifyError(@() phy.reg_read(16), 'iio:ENOENT');     % never written
            phy.reg_write(16, 0xCAFE);
            v = phy.reg_read(16);
            tc.verifyClass(v, 'uint32');
            tc.verifySize(v, [1 1]);
            tc.verifyEqual(v, uint32(51966));
            tc.verifyError(@() phy.reg_read(17), 'iio:ENOENT');
            phy.reg_write(uint32(16), int32(7));
            tc.verifyEqual(phy.reg_read(uint8(16)), uint32(7));
        end

        function registerRange(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            phy.reg_write(4294967295, 4294967295);
            tc.verifyEqual(phy.reg_read(4294967295), uint32(4294967295));
            tc.verifyError(@() phy.reg_read(2^32), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_read(-1), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_read(1.5), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_write(2^32, 1), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_write(16, 2^32), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_write(16, -1), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_write(16, 1.5), 'iio:EINVAL');
            tc.verifyError(@() phy.reg_read(16), 'iio:ENOENT');     % none of them wrote
        end

        function debugAttrs(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            rx = ctx.find_device("emu-rx");
            d = phy.debug_attrs;
            tc.verifyClass(d, 'iio.Attr');
            tc.verifySize(d, [1 4]);
            tc.verifyEqual(string({d.name}), ...
                ["bist_prbs" "direct_reg_access" "loopback" "loopback_available"]);
            a = phy.find_debug_attr("loopback");
            tc.verifyClass(a, 'iio.Attr');
            tc.verifySize(a, [1 1]);
            tc.verifyEqual(a.name, 'loopback');
            tc.verifyTrue(a == d(3));
            tc.verifyTrue(phy.find_debug_attr('bist_prbs') == d(1));
            none = phy.find_debug_attr("nope");
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Attr');
            tc.verifyTrue(isempty(phy.find_debug_attr("gain")));    % a device attribute
            tc.verifyTrue(isempty(phy.find_attr("loopback")));      % and the other way round
            noDebug = rx.debug_attrs;
            tc.verifyTrue(isempty(noDebug));
            tc.verifyClass(noDebug, 'iio.Attr');
            tc.verifySize(noDebug, [1 0]);
        end

        function eventAttrs(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            rx = ctx.find_device("emu-rx");
            e = phy.event_attrs;
            tc.verifyClass(e, 'iio.Attr');
            tc.verifySize(e, [1 1]);
            tc.verifyEqual(e.name, 'events_enabled');
            tc.verifyTrue(phy.find_event_attr("events_enabled") == e);
            none = phy.find_event_attr("loopback");                 % a debug attribute
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Attr');
            tc.verifyTrue(isempty(phy.find_debug_attr("events_enabled")));
            noEvents = rx.event_attrs;
            tc.verifyClass(noEvents, 'iio.Attr');
            tc.verifySize(noEvents, [1 0]);
        end

        function triggerOnEmu(tc)
            % the emulated backend has no trigger operations
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            phy = ctx.find_device("emu-phy");
            trg = ctx.find_device("trigger0");
            tc.verifyError(@() rx.get_trigger(), 'iio:ENOSYS');
            tc.verifyError(@() rx.set_trigger(trg), 'iio:ENOSYS');
            tc.verifyError(@() rx.set_trigger([]), 'iio:ENOSYS');
            tc.verifyError(@() rx.set_trigger(phy), 'iio:EINVAL');  % not a trigger
            tc.verifyError(@() rx.set_trigger(5), 'iio:EINVAL');    % not a device
        end

        function triggerFake(tc)
            ctx = tc.Fx.fakeContext();
            rx = ctx.find_device("emu-rx");
            phy = ctx.find_device("emu-phy");
            trg = ctx.find_device("trigger0");
            none = rx.get_trigger();
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Device');
            rx.set_trigger(trg);
            t = rx.get_trigger();
            tc.verifyClass(t, 'iio.Device');
            tc.verifySize(t, [1 1]);
            tc.verifyEqual(t.id, 'trigger0');
            tc.verifyTrue(t == trg);
            tc.verifyTrue(t.context == ctx);
            tc.verifyError(@() rx.set_trigger(phy), 'iio:EINVAL');  % not a trigger: the old one stays
            t2 = rx.get_trigger();
            tc.verifyEqual(t2.id, 'trigger0');
            rx.set_trigger([]);
            tc.verifyTrue(isempty(rx.get_trigger()));
        end

        function triggerOfAnotherContext(tc)
            ctx = tc.Fx.fakeContext();
            other = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            foreign = other.find_device("trigger0");
            tc.verifyError(@() rx.set_trigger(foreign), 'iio:EINVAL');
            tc.verifyTrue(isempty(rx.get_trigger()));
        end
    end
end
