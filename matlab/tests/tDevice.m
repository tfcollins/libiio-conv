classdef tDevice < IioTestCase
    %TDEVICE iio.Device: identity, flags, channel, attribute and buffer lists, equality, lifetime.

    methods (Test)
        function identity(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            tc.verifyClass(d, 'iio.Device');
            tc.verifySize(d, [1 1]);
            tc.verifyEqual(d.id, 'iio:device2');
            tc.verifyEqual(d.name, 'emu-phy');
            tc.verifyEqual(d.label, 'phy');
            % no name and no label: '' of class char
            e = ctx.find_device("iio:device4");
            tc.verifyEqual(e.id, 'iio:device4');
            tc.verifyClass(e.name, 'char');
            tc.verifyClass(e.label, 'char');
            tc.verifyEqual(e.name, '');
            tc.verifyEqual(e.label, '');
        end

        function order(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.devices;
            tc.verifyEqual({d.id}, {'hwmon0', 'iio:device0', 'iio:device1', 'iio:device2', ...
                'iio:device3', 'iio:device4', 'iio_sysfs_trigger', 'trigger0'});
        end

        function context(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            tc.verifyTrue(isa(d.context, 'iio.Context'));
            tc.verifyTrue(d.context == ctx);
        end

        function flags(tc)
            ctx = iio.Context(tc.Fx.Uri);
            t = ctx.find_device("trigger0").is_trigger;
            tc.verifyClass(t, 'logical');
            tc.verifyEqual(t, true);
            tc.verifyEqual(ctx.find_device("iio_sysfs_trigger").is_trigger, false);
            h = ctx.find_device("hwmon0").is_hwmon;
            tc.verifyClass(h, 'logical');
            tc.verifyEqual(h, true);
            tc.verifyEqual(ctx.find_device("emu-rx").is_hwmon, false);
        end

        function channels(tc)
            ctx = iio.Context(tc.Fx.Uri);
            ch = ctx.find_device("emu-rx").channels;
            tc.verifyClass(ch, 'iio.Channel');
            tc.verifySize(ch, [1 5]);
            tc.verifySize(ctx.find_device("emu-phy").channels, [1 6]);
            none = ctx.find_device("trigger0").channels;
            tc.verifyClass(none, 'iio.Channel');
            tc.verifySize(none, [1 0]);
        end

        function findChannel(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            chIn = d.find_channel("voltage0");
            chOut = d.find_channel("voltage0", true);
            tc.verifyClass(chIn, 'iio.Channel');
            tc.verifySize(chIn, [1 1]);
            tc.verifySize(chOut, [1 1]);
            tc.verifyTrue(chIn ~= chOut);
            tc.verifyTrue(d.find_channel("RX1") == chIn);
            tc.verifyTrue(d.find_channel('rx1-port') == d.find_channel("voltage0", false));
            none = d.find_channel("nope");
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Channel');
            % scan-element channels: their order is defined
            rx = ctx.find_device("emu-rx");
            ch = rx.channels;
            tc.verifyTrue(rx.find_channel("voltage0_i") == ch(1));
            tc.verifyTrue(rx.find_channel("voltage2") == ch(5));
            tc.verifyTrue(isempty(rx.find_channel("voltage0_i", true)));
        end

        function attrs(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            a = d.attrs;
            tc.verifyClass(a, 'iio.Attr');
            tc.verifySize(a, [1 14]);
            tc.verifyEqual({a.name}, {'calib_mode', 'calib_mode_available', 'dbl', 'empty', ...
                'ensm_mode', 'ensm_mode_available', 'flag', 'frac', 'frac_available', 'gain', ...
                'gain_available', 'hex', 'novalue', 'text'});
            tc.verifyTrue(d.find_attr("gain") == a(10));
            none = d.find_attr("bist_prbs");    % a debug attribute
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Attr');
            rxAttrs = ctx.find_device("emu-rx").attrs;
            tc.verifySize(rxAttrs, [1 0]);
            tc.verifyClass(rxAttrs, 'iio.Attr');
        end

        function buffers(tc)
            ctx = iio.Context(tc.Fx.Uri);
            b = ctx.find_device("emu-multi").buffers;
            tc.verifyClass(b, 'iio.Buffer');
            tc.verifySize(b, [1 2]);
            none = ctx.find_device("emu-phy").buffers;
            tc.verifyClass(none, 'iio.Buffer');
            tc.verifySize(none, [1 0]);
        end

        function equality(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            devs = ctx.devices;
            tc.verifyTrue(devs(4) == d);
            tc.verifyEqual(devs == d, logical([0 0 0 1 0 0 0 0]));
            tc.verifyTrue(d ~= devs(1));
        end

        function stale(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            delete(ctx);
            tc.verifyEqual(d.id, 'iio:device2');
            tc.verifyError(@() d.channels, 'iio:EBADF');
            tc.verifyError(@() d.context, 'iio:EBADF');
        end

        function lowLevelMembersExist(tc)
            % these call the commands of the low-level group; their behaviour is tested there
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            names = {'sample_size', 'get_trigger', 'set_trigger', 'reg_read', 'reg_write', ...
                'find_debug_attr', 'find_event_attr', 'create_event_stream'};
            for k = 1:numel(names)
                tc.verifyTrue(ismethod(d, names{k}), names{k});
            end
            tc.verifyTrue(isprop(d, 'debug_attrs'));
            tc.verifyTrue(isprop(d, 'event_attrs'));
        end
    end
end
