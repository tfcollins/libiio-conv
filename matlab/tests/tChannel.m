classdef tChannel < IioTestCase
    %TCHANNEL iio.Channel and iio.ChannelsMask: identity, type names, attributes, data
    %   format, sample conversion, masks, lifetime.

    methods (Test)
        function identity(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v2 = rx.find_channel("voltage2");
            tc.verifyClass(v2, 'iio.Channel');
            tc.verifySize(v2, [1 1]);
            tc.verifyClass(v2.id, 'char');
            tc.verifyEqual(v2.id, 'voltage2');
            tc.verifyEqual(v2.name, 'aux');
            tc.verifyEqual(v2.label, 'aux-adc');
            tc.verifyEqual(v2.output, false);
            tc.verifyEqual(v2.scan_element, true);
            tc.verifyEqual(v2.index, 4);
            tc.verifyEqual(v2.type, 'IIO_VOLTAGE');
            tc.verifyEqual(v2.type_id, 0);
            tc.verifyEqual(v2.modifier, 'IIO_NO_MOD');
            tc.verifyEqual(v2.modifier_id, 0);
            tc.verifyTrue(isa(v2.device, 'iio.Device'));
            tc.verifyTrue(v2.device == rx);

            % no name and no label: '' of class char
            v0i = rx.find_channel("voltage0_i");
            tc.verifyClass(v0i.name, 'char');
            tc.verifyClass(v0i.label, 'char');
            tc.verifyEqual(v0i.name, '');
            tc.verifyEqual(v0i.label, '');
            tc.verifyEqual(v0i.index, 0);
            tc.verifyEqual(v0i.modifier, 'IIO_MOD_I');
            tc.verifyEqual(v0i.modifier_id, 32);
            v0q = rx.find_channel("voltage0_q");
            tc.verifyEqual(v0q.modifier, 'IIO_MOD_Q');
            tc.verifyEqual(v0q.modifier_id, 33);

            % an output channel that is not a scan element
            alt = ctx.find_device("emu-tx").find_channel("altvoltage0", true);
            tc.verifyEqual(alt.id, 'altvoltage0');
            tc.verifyEqual(alt.name, 'TX1_I_F1');
            tc.verifyEqual(alt.label, '');
            tc.verifyEqual(alt.output, true);
            tc.verifyEqual(alt.scan_element, false);
            tc.verifyEqual(alt.index, -2);
            tc.verifyEqual(alt.type, 'IIO_ALTVOLTAGE');
            tc.verifyEqual(alt.type_id, 15);

            accel = ctx.find_device("emu-phy").find_channel("accel_x");
            tc.verifyEqual(accel.type, 'IIO_ACCEL');
            tc.verifyEqual(accel.type_id, 3);
            tc.verifyEqual(accel.modifier, 'IIO_MOD_X');
            tc.verifyEqual(accel.modifier_id, 1);
        end

        function order(tc)
            ctx = iio.Context(tc.Fx.Uri);
            ch = ctx.find_device("emu-rx").channels;
            tc.verifyEqual({ch.id}, {'voltage0_i', 'voltage0_q', 'voltage1_i', 'voltage1_q', 'voltage2'});
            tc.verifyEqual([ch.index], [0 1 2 3 4]);
            tc.verifyEqual([ch.scan_element], [true true true true true]);
            tc.verifyEqual([ch.output], [false false false false false]);
            ph = ctx.find_device("emu-phy").channels;
            tc.verifyEqual({ph.id}, {'voltage0', 'voltage0', 'altvoltage0', 'altvoltage1', 'temp0', 'accel_x'});
            tc.verifyEqual([ph.output], [false true true true false false]);
            tc.verifyEqual([ph.scan_element], [false false false false false false]);
            tc.verifyEqual({ph.type}, {'IIO_VOLTAGE', 'IIO_VOLTAGE', 'IIO_ALTVOLTAGE', 'IIO_ALTVOLTAGE', ...
                'IIO_TEMP', 'IIO_ACCEL'});
        end

        function hwmonType(tc)
            ctx = iio.Context(tc.Fx.Uri);
            hw = ctx.find_device("hwmon0");
            t = hw.find_channel("temp1");
            tc.verifyEqual(t.type, 'HWMON_TEMP');
            tc.verifyEqual(t.type_id, 3);
            in1 = hw.find_channel("in1");
            tc.verifyEqual(in1.type, 'HWMON_VOLTAGE');
            tc.verifyEqual(in1.type_id, 0);
        end

        function attrs(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            v0q = rx.find_channel("voltage0_q");
            v2 = rx.find_channel("voltage2");
            a = v0i.attrs;
            tc.verifyClass(a, 'iio.Attr');
            tc.verifySize(a, [1 2]);
            tc.verifyEqual(numel(v0i.attrs), 2);
            none = v0q.attrs;
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Attr');
            tc.verifySize(none, [1 0]);
            a2 = v2.attrs;
            tc.verifyEqual({a2.name}, {'offset', 'raw', 'scale'});
            tc.verifyTrue(v2.find_attr("scale") == a2(3));
            tc.verifyTrue(v2.find_attr("scale") == v2.attrs(3));
            nope = v2.find_attr("nope");
            tc.verifyTrue(isempty(nope));
            tc.verifyClass(nope, 'iio.Attr');
            tc.verifySize(nope, [1 0]);
            tc.verifySize(v2.event_attrs, [1 0]);
            tc.verifyClass(v2.event_attrs, 'iio.Attr');

            % the same id in both directions; a name prefix; event attributes
            phy = ctx.find_device("emu-phy");
            in0 = phy.find_channel("voltage0");
            out0 = phy.find_channel("voltage0", true);
            gain = in0.find_attr("hardwaregain");
            tc.verifySize(gain, [1 1]);
            tc.verifyEqual(gain.name, 'hardwaregain');
            tc.verifyTrue(in0.find_attr("RX1_hardwaregain") == gain);
            tc.verifyTrue(out0.find_attr("hardwaregain") ~= gain);
            ev = in0.event_attrs;
            tc.verifyClass(ev, 'iio.Attr');
            tc.verifySize(ev, [1 3]);
            en = in0.find_event_attr("thresh_rising_en");
            tc.verifySize(en, [1 1]);
            tc.verifyEqual(en.name, 'thresh_rising_en');
            tc.verifyEqual(nnz(ev == en), 1);
            tc.verifyTrue(isempty(in0.find_attr("thresh_rising_en")));
            tc.verifyTrue(isempty(in0.find_event_attr("hardwaregain")));
        end

        function dataFormat(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            f = rx.find_channel("voltage0_i").data_format;
            tc.verifyClass(f, 'struct');
            tc.verifySize(f, [1 1]);
            tc.verifyEqual(f, struct('length', 16, 'bits', 12, 'shift', 0, 'repeat', 1, ...
                'is_signed', true, 'is_fully_defined', false, 'is_be', false, 'with_scale', false, ...
                'is_float', false, 'scale', 0, 'offset', 0));
            g = rx.find_channel("voltage2").data_format;
            tc.verifyEqual(g, struct('length', 16, 'bits', 14, 'shift', 2, 'repeat', 1, ...
                'is_signed', true, 'is_fully_defined', false, 'is_be', false, 'with_scale', true, ...
                'is_float', false, 'scale', 0.25, 'offset', -100));
            % not a scan element: no storage format
            p = ctx.find_device("emu-phy").find_channel("voltage0").data_format;
            tc.verifyEqual(p.length, 0);
        end

        function refreshFormat(tc)
            ctx = iio.Context(tc.Fx.Uri);
            v0i = ctx.find_device("emu-rx").find_channel("voltage0_i");
            tc.verifyError(@() v0i.refresh_format(), 'iio:ENOSYS');
        end

        function convert(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d4 = ctx.find_device("iio:device4");
            v2 = d4.find_channel("voltage2");       % le:s12/16>>4
            out = v2.convert(uint8([0 240]));
            tc.verifyClass(out, 'uint8');
            tc.verifyEqual(out, uint8([0; 255]));
            tc.verifyEqual(v2.convert_inverse(uint8([0 255])), uint8([0; 240]));
            tc.verifyEqual(v2.convert(uint8([0 240 0 240])), uint8([0; 255; 0; 255]));
            tc.verifyEqual(v2.convert(uint8([0; 240])), uint8([0; 255]));       % a column works too
            v1 = d4.find_channel("voltage1");       % be:u24/32>>8
            tc.verifyEqual(v1.convert(uint8([18 52 86 120])), uint8([86; 52; 18; 0]));
            tc.verifyEqual(v1.convert_inverse(uint8([86 52 18 0])), uint8([18; 52; 86; 0]));
            % nothing in, nothing out
            none = v2.convert(uint8.empty(1, 0));
            tc.verifyClass(none, 'uint8');
            tc.verifySize(none, [0 1]);
            tc.verifySize(v2.convert_inverse(uint8.empty(1, 0)), [0 1]);
            % not whole samples
            tc.verifyError(@() v2.convert(uint8([0 240 0])), 'iio:EINVAL');
            tc.verifyError(@() v2.convert_inverse(uint8([0 240 0])), 'iio:EINVAL');
            % a channel that is not a scan element has no sample format
            p = ctx.find_device("emu-phy").find_channel("voltage0");
            tc.verifyError(@() p.convert(uint8([0 1])), 'iio:EINVAL');
            tc.verifyError(@() p.convert_inverse(uint8([0 1])), 'iio:EINVAL');
        end

        function maskRoundTrip(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            ch = rx.channels;
            v0i = ch(1);
            v0q = ch(2);
            v2 = ch(5);
            mask = iio.ChannelsMask(rx);
            tc.verifyClass(mask, 'iio.ChannelsMask');
            tc.verifyTrue(mask.device == rx);
            e = mask.enabled();
            tc.verifyClass(e, 'logical');
            tc.verifyEqual(e, [false false false false false]);
            tc.verifyEqual(mask.sample_size, 0);
            tc.verifyClass(mask.channels, 'iio.Channel');
            tc.verifySize(mask.channels, [1 0]);

            mask.channels = [v0i v0q];
            tc.verifyEqual(mask.enabled(), [true true false false false]);
            tc.verifyEqual(mask.sample_size, 4);
            tc.verifySize(mask.channels, [1 2]);
            tc.verifyTrue(isequal(mask.channels == [v0i v0q], [true true]));
            tf = v0i.is_enabled(mask);
            tc.verifyClass(tf, 'logical');
            tc.verifyEqual(tf, true);
            tc.verifyEqual(v2.is_enabled(mask), false);

            v0i.disable(mask);
            tc.verifyEqual(v0i.is_enabled(mask), false);
            tc.verifyEqual(mask.enabled(), [false true false false false]);
            v2.enable(mask);
            tc.verifyEqual(mask.enabled(), [false true false false true]);
            tc.verifyEqual(mask.sample_size, 4);
            tc.verifyTrue(isequal(mask.channels == [v0q v2], [true true]));     % device order

            % assigning enables exactly the given channels, whatever was enabled before
            mask.channels = v0i;
            tc.verifyEqual(mask.enabled(), [true false false false false]);
            tc.verifyEqual(mask.sample_size, 2);
            enable(ch(3:4), mask);      % a row of channels
            tc.verifyEqual(mask.enabled(), [true false true true false]);
            disable(ch(1:3), mask);
            tc.verifyEqual(mask.enabled(), [false false false true false]);
            mask.channels = [];
            tc.verifyEqual(mask.enabled(), [false false false false false]);
            mask.channels = [v2 v0q];   % the order given does not matter
            tc.verifyEqual(mask.enabled(), [false true false false true]);
        end

        function maskWrongDevice(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            tx = ctx.find_device("emu-tx");
            v0i = rx.find_channel("voltage0_i");
            v0q = rx.find_channel("voltage0_q");
            txi = tx.find_channel("voltage0_i", true);
            alt = tx.find_channel("altvoltage0", true);
            mask = iio.ChannelsMask(rx);
            tc.verifyError(@() txi.enable(mask), 'iio:EINVAL');
            tc.verifyError(@() txi.disable(mask), 'iio:EINVAL');
            tc.verifyError(@() txi.is_enabled(mask), 'iio:EINVAL');
            % a failed assignment leaves the mask as it was
            mask.channels = v0q;
            tc.verifyError(@() setChannels(mask, [v0i txi]), 'iio:EINVAL');
            tc.verifyEqual(mask.enabled(), [false true false false false]);
            tc.verifyError(@() setChannels(mask, "voltage0_i"), 'iio:EINVAL');
            tc.verifyEqual(mask.enabled(), [false true false false false]);
            % a channel that is not a scan element cannot be enabled
            txmask = iio.ChannelsMask(tx);
            tc.verifyError(@() alt.enable(txmask), 'iio:EINVAL');
            tc.verifyError(@() setChannels(txmask, [txi alt]), 'iio:EINVAL');
            tc.verifyEqual(txmask.enabled(), [false false false false false]);
            tc.verifyEqual(alt.is_enabled(txmask), false);
            % a device without channels has no mask; a mask is made from one device
            trig = ctx.find_device("trigger0");
            tc.verifyError(@() iio.ChannelsMask(trig), 'iio:EINVAL');
            tc.verifyError(@() iio.ChannelsMask(ctx), 'iio:EINVAL');
        end

        function maskArgumentIsNotConverted(tc)
            % A method that takes a mask refuses everything else. MATLAB must not turn
            % a device into iio.ChannelsMask(dev), a new and empty mask, and work on that.
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            mask = iio.ChannelsMask(rx);
            v0i.enable(mask);
            n = iio.internal.handle_count();
            tc.verifyValidationError(@() v0i.enable(rx));
            tc.verifyValidationError(@() v0i.disable(rx));
            tc.verifyValidationError(@() v0i.is_enabled(rx));
            tc.verifyValidationError(@() rx.sample_size(rx));
            tc.verifyValidationError(@() v0i.is_enabled(5));
            tc.verifyValidationError(@() v0i.enable("mask"));
            tc.verifyValidationError(@() rx.sample_size(v0i));
            tc.verifyValidationError(@() v0i.disable([mask mask]));    % one mask
            tc.verifyEqual(iio.internal.handle_count(), n);
            tc.verifyEqual(mask.enabled(), [true false false false false]);
            tc.verifyEqual(rx.sample_size(mask), 2);
            tc.verifyEqual(v0i.is_enabled(mask), true);
        end

        function enableNoChannel(tc)
            % find_channel of an id the device does not have gives a 1-by-0 row:
            % enabling "it" is an error, not a silent nothing
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            mask = iio.ChannelsMask(rx);
            typo = rx.find_channel("voltage9");
            tc.verifySize(typo, [1 0]);
            tc.verifyError(@() typo.enable(mask), 'iio:EINVAL');
            tc.verifyError(@() typo.disable(mask), 'iio:EINVAL');
            tc.verifyError(@() enable(iio.Channel.empty(1, 0), mask), 'iio:EINVAL');
            tc.verifyEqual(mask.enabled(), [false false false false false]);
        end

        function maskDestroyTwice(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v0i = rx.find_channel("voltage0_i");
            mask = iio.ChannelsMask(rx);
            before = iio.internal.handle_count();
            mask.destroy();
            tc.verifyEqual(iio.internal.handle_count(), before - 1);
            mask.destroy();
            tc.verifyEqual(iio.internal.handle_count(), before - 1);
            tc.verifyError(@() mask.enabled(), 'iio:EBADF');
            tc.verifyError(@() v0i.enable(mask), 'iio:EBADF');
            tc.verifyError(@() v0i.is_enabled(mask), 'iio:EBADF');
            tc.verifyError(@() mask.channels, 'iio:EBADF');
            tc.verifyTrue(mask.device == rx);
        end

        function stale(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            v2 = rx.find_channel("voltage2");
            mask = iio.ChannelsMask(rx);
            delete(ctx);
            tc.verifyEqual(v2.id, 'voltage2');
            tc.verifyEqual(v2.index, 4);
            tc.verifyError(@() v2.type, 'iio:EBADF');
            tc.verifyError(@() v2.device, 'iio:EBADF');
            tc.verifyError(@() v2.attrs, 'iio:EBADF');
            tc.verifyError(@() v2.data_format, 'iio:EBADF');
            tc.verifyError(@() mask.enabled(), 'iio:EBADF');
            tc.verifyError(@() v2.enable(mask), 'iio:EBADF');
            mask.destroy();     % silent on a mask that went with its context
        end

        function withoutHandle(tc)
            % the forms MATLAB itself uses for object arrays: no gateway call, no handle
            c = iio.Channel();
            tc.verifyEqual(c.id, '');
            tc.verifyEqual(c.name, '');
            tc.verifyEqual(c.output, false);
            tc.verifyEqual(iio.internal.Object.handles(c), uint64(0));
            m = iio.ChannelsMask();
            tc.verifyEqual(iio.internal.Object.handles(m), uint64(0));
            tc.verifyError(@() m.device, 'iio:EBADF');
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function stringInput(tc)
            ctx = iio.Context(tc.Fx.Uri);
            v2 = ctx.find_device("emu-rx").find_channel("voltage2");
            a = v2.find_attr("scale");
            b = v2.find_attr('scale');
            tc.verifySize(a, [1 1]);
            tc.verifySize(b, [1 1]);
            tc.verifyTrue(a == b);
            in0 = ctx.find_device("emu-phy").find_channel("voltage0");
            e1 = in0.find_event_attr("thresh_rising_en");
            e2 = in0.find_event_attr('thresh_rising_en');
            tc.verifySize(e1, [1 1]);
            tc.verifyTrue(e1 == e2);
            tc.verifyTrue(isempty(v2.find_attr("")));
            % text with a zero character is refused, not cut to 'scale'
            tc.verifyError(@() v2.find_attr(['scale' char(0) 'x']), 'iio:EINVAL');
            tc.verifyError(@() in0.find_event_attr(['thresh_rising_en' char(0)]), 'iio:EINVAL');
        end
    end
end

function setChannels(mask, chans)
%SETCHANNELS mask.channels = chans, as a function for verifyError.
mask.channels = chans;
end
