classdef tAttr < IioTestCase
    %TATTR iio.Attr: names, text and byte reads, typed reads and writes, exact numbers, binary payloads.

    methods
        function a = phyAttr(~, ctx, name)
            %PHYATTR Attribute NAME of the device emu-phy.
            a = ctx.find_device("emu-phy").find_attr(name);
        end
    end

    methods (Test)
        function names(tc)
            ctx = iio.Context(tc.Fx.Uri);
            a = tc.phyAttr(ctx, "calib_mode");
            tc.verifyClass(a, 'iio.Attr');
            tc.verifySize(a, [1 1]);
            tc.verifyEqual(a.name, 'calib_mode');
            tc.verifyEqual(a.filename, 'calib_mode');
            tc.verifyTrue(isa(a.static_value, 'double'));
            tc.verifyTrue(isempty(a.static_value));
            tc.verifyEqual(ctx.find_attr("hw_model").static_value, 'Emulated Test Board');
        end

        function read(tc)
            ctx = iio.Context(tc.Fx.Uri);
            a = tc.phyAttr(ctx, "calib_mode");
            tc.verifyEqual(a.read(), 'auto');
            tc.verifyEqual(a.value, 'auto');
            tc.verifyEqual(a.read_raw(), uint8([97 117 116 111 0]));
            tc.verifyEqual(tc.phyAttr(ctx, "empty").read(), '');
            novalue = tc.phyAttr(ctx, "novalue");
            tc.verifyError(@() novalue.read(), 'iio:ENOENT');
        end

        function typedRead(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tf = tc.phyAttr(ctx, "flag").read_bool();
            tc.verifyClass(tf, 'logical');
            tc.verifyEqual(tf, true);
            v = tc.phyAttr(ctx, "hex").read_longlong();
            tc.verifyClass(v, 'int64');
            tc.verifyEqual(v, int64(16));
            d = tc.phyAttr(ctx, "dbl").read_double();
            tc.verifyClass(d, 'double');
            tc.verifyEqual(d, 3.25);
            text = tc.phyAttr(ctx, "text");
            tc.verifyError(@() text.read_longlong(), 'iio:EINVAL');
        end

        function writeString(tc)
            ctx = iio.Context(tc.Fx.Uri);
            a = tc.phyAttr(ctx, "calib_mode");
            a.write_string("manual");
            tc.verifyEqual(a.read(), 'manual');
            a.value = 'auto';
            tc.verifyEqual(a.read(), 'auto');
            a.write_string('tx_quad');
            tc.verifyEqual(a.value, 'tx_quad');
            tc.verifyError(@() a.write_string("bogus"), 'iio:EINVAL');
            tc.verifyEqual(a.read(), 'tx_quad');
            avail = tc.phyAttr(ctx, "calib_mode_available");
            tc.verifyError(@() avail.write_string("x"), 'iio:EACCES');
            model = ctx.find_attr("hw_model");
            tc.verifyError(@() model.write_string("x"), 'iio:EPERM');
        end

        function int64Exact(tc)
            % no value passes through double: 2^53 + 1 is not a double
            ctx = iio.Context(tc.Fx.Uri);
            hex = tc.phyAttr(ctx, "hex");
            hex.write_longlong(int64(9007199254740993));
            tc.verifyEqual(hex.read(), '9007199254740993');
            tc.verifyEqual(hex.read_longlong(), int64(9007199254740993));
            hex.write_longlong(intmin('int64'));
            tc.verifyEqual(hex.read_longlong(), intmin('int64'));
            hex.write_longlong(uint64(2450000000));
            tc.verifyEqual(hex.read(), '2450000000');
            tc.verifyError(@() hex.write_longlong(2.5), 'iio:EINVAL');
            tc.verifyError(@() hex.write_longlong(intmax('uint64')), 'iio:EINVAL');
            tc.verifyEqual(hex.read(), '2450000000');
        end

        function doubleExact(tc)
            ctx = iio.Context(tc.Fx.Uri);
            dbl = tc.phyAttr(ctx, "dbl");
            dbl.write_double(1e-9);
            tc.verifyEqual(dbl.read(), '0.000000001');
            dbl.write_double(0.1);
            tc.verifyEqual(dbl.read(), '0.1');
            dbl.write_double(2.4e9);
            tc.verifyEqual(dbl.read(), '2400000000');
            dbl.write_double(1e-9, Exact = false);
            tc.verifyEqual(dbl.read(), '0.000000');
            tc.verifyError(@() dbl.write_double(NaN), 'iio:EINVAL');
            tc.verifyEqual(dbl.read(), '0.000000');
        end

        function writeDispatch(tc)
            ctx = iio.Context(tc.Fx.Uri);
            text = tc.phyAttr(ctx, "text");
            text.write("abc");
            tc.verifyEqual(text.read(), 'abc');
            text.write('def');
            tc.verifyEqual(text.read(), 'def');
            text.write(true);
            tc.verifyEqual(text.read(), '1');
            text.write_bool(false);
            tc.verifyEqual(text.read(), '0');
            text.write(17);
            tc.verifyEqual(text.read(), '17');
            text.write(int32(-5));
            tc.verifyEqual(text.read(), '-5');
            text.write(0.25);
            tc.verifyEqual(text.read(), '0.25');
            text.write(2400000000);
            tc.verifyEqual(text.read(), '2400000000');
            text.value = 42;
            tc.verifyEqual(text.value, '42');
        end

        function writeRaw(tc)
            % a binary payload goes by its byte count (the emu half; the fake backend shows the bytes)
            ctx = iio.Context(tc.Fx.Uri);
            text = tc.phyAttr(ctx, "text");
            n = text.write_raw(uint8('abcdef'));
            tc.verifyClass(n, 'double');
            tc.verifyEqual(n, 6);
            tc.verifyEqual(text.read(), 'abcdef');
            p = uint8(97 + mod(0:46999, 26));
            tc.verifyEqual(text.write_raw(p), 47000);
            tc.verifyEqual(text.read(), char(p));
            tc.verifyEqual(numel(text.read_raw()), 47001);
            tc.verifyEqual(text.write_raw([65 66 67]), 3);      % a double row: converted to uint8
            tc.verifyEqual(text.read(), 'ABC');
            tc.verifyEqual(text.write_raw(uint8([68; 69])), 2); % a column
            tc.verifyEqual(text.read(), 'DE');
        end

        function available(tc)
            ctx = iio.Context(tc.Fx.Uri);
            modes = tc.phyAttr(ctx, "calib_mode_available");
            tc.verifyEqual(modes.get_available(), {'auto', 'manual', 'tx_quad', 'rf_dc_offs'});
            tc.verifyEqual(tc.phyAttr(ctx, "gain_available").get_range(), [0 2 70]);
            tc.verifyError(@() modes.get_range(), 'iio:ENOTSUP');
            mode = tc.phyAttr(ctx, "calib_mode");
            tc.verifyError(@() mode.get_available(), 'iio:ENXIO');
        end

        function xmlBackend(tc)
            ctx = iio.Context(tc.Fx.XmlUri);
            a = tc.phyAttr(ctx, "calib_mode");
            tc.verifyEqual(a.name, 'calib_mode');
            tc.verifyError(@() a.read(), 'iio:ENOSYS');
        end

        function stale(tc)
            ctx = iio.Context(tc.Fx.Uri);
            a = tc.phyAttr(ctx, "calib_mode");
            delete(ctx);
            tc.verifyError(@() a.read(), 'iio:EBADF');
            tc.verifyEqual(a.name, 'calib_mode');
        end
    end
end
