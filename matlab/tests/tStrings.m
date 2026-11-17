classdef tStrings < IioTestCase
    %TSTRINGS Text arguments in every form MATLAB has: char and string give the same
    %   result, channel lists may be string arrays, cell arrays or one char row, a
    %   missing string is an error, an empty or non-ASCII name finds nothing, and text
    %   with a zero character is refused by the gateway, never cut there.

    methods (Test)
        function charAndStringAgree(tc)
            c1 = iio.Context(tc.Fx.Uri);                    % a char row
            c2 = iio.Context(string(tc.Fx.Uri));
            tc.verifyClass(c2.uri, 'char');
            tc.verifyEqual(c2.uri, c1.uri);
            tc.verifyEqual(c2.name, c1.name);
            tc.verifyEqual(c2.description, c1.description);
            tc.verifyEqual(numel(c2.devices), numel(c1.devices));
            tc.verifyEqual(c2.xml, c1.xml);
            delete(c2);

            % find_device, by name, by id and by label
            d1 = c1.find_device('emu-phy');
            d2 = c1.find_device("emu-phy");
            tc.verifySize(d1, [1 1]);
            tc.verifyTrue(d1 == d2);
            tc.verifyEqual(d2.id, 'iio:device2');
            tc.verifyTrue(c1.find_device('iio:device2') == d1);
            tc.verifyTrue(c1.find_device("phy") == d1);

            % find_channel, with and without the direction
            ch1 = d1.find_channel('voltage0');
            ch2 = d1.find_channel("voltage0");
            tc.verifySize(ch1, [1 1]);
            tc.verifyTrue(ch1 == ch2);
            tc.verifyEqual(ch2.id, 'voltage0');
            o1 = d1.find_channel('voltage0', true);
            o2 = d1.find_channel("voltage0", true);
            tc.verifyTrue(o1 == o2);
            tc.verifyTrue(o1 ~= ch1);

            % find_attr of a context, a device and a channel
            tc.verifyTrue(c1.find_attr('hw_model') == c1.find_attr("hw_model"));
            tc.verifyEqual(c1.find_attr('hw_model').read(), 'Emulated Test Board');
            a1 = d1.find_attr('text');
            a2 = d1.find_attr("text");
            tc.verifySize(a1, [1 1]);
            tc.verifyTrue(a1 == a2);
            tc.verifyEqual(a2.name, 'text');
            v2 = c1.find_device('emu-rx').find_channel("voltage2");
            tc.verifyTrue(v2.find_attr('scale') == v2.find_attr("scale"));
            tc.verifySize(v2.find_attr("scale"), [1 1]);
            tc.verifyTrue(d1.find_debug_attr('loopback') == d1.find_debug_attr("loopback"));
            tc.verifySize(d1.find_debug_attr("loopback"), [1 1]);

            % attr.write and write_string
            a1.write('abc');
            tc.verifyEqual(a1.read(), 'abc');
            a1.write("abd");
            tc.verifyEqual(a2.read(), 'abd');
            a1.write_string('x y');
            tc.verifyEqual(a1.read(), 'x y');
            a1.write_string("x z");
            tc.verifyEqual(a1.read(), 'x z');
            a1.write("");
            tc.verifyEqual(a1.read(), '');
            a1.write('');
            tc.verifyEqual(a1.read(), '');
        end

        function channelListForms(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            want = [(-2048:-2041).', (2047:-1:2040).'];
            forms = {["voltage0_i" "voltage0_q"], ["voltage0_i"; "voltage0_q"], ...
                {'voltage0_i', 'voltage0_q'}, {'voltage0_i'; 'voltage0_q'}};
            for k = 1:numel(forms)
                r = iio.RxStream(rx, forms{k}, SamplesPerFrame = 8);
                [d, valid] = r.read();
                ids = r.channels;
                r.close();
                tc.verifyEqual(valid, true, sprintf('form %d', k));
                tc.verifyEqual(d, want, sprintf('form %d', k));
                tc.verifyEqual(ids, {'voltage0_i', 'voltage0_q'}, sprintf('form %d', k));
            end
            % one channel: a string, a char row, a cell
            want = double(EmuFixture.s14shift2(EmuFixture.rampWords(4))).';
            forms = {"voltage2", 'voltage2', {'voltage2'}};
            for k = 1:numel(forms)
                r = iio.RxStream(rx, forms{k}, SamplesPerFrame = 8);
                [d, valid] = r.read();
                ids = r.channels;
                r.close();
                tc.verifyEqual(valid, true, sprintf('one channel, form %d', k));
                tc.verifyEqual(d, want, sprintf('one channel, form %d', k));
                tc.verifyEqual(ids, {'voltage2'}, sprintf('one channel, form %d', k));
            end
            % the same for a transmit stream
            tx = ctx.find_device("emu-tx");
            forms = {["voltage0_i" "voltage0_q"], {'voltage0_i', 'voltage0_q'}};
            for k = 1:numel(forms)
                t = iio.TxStream(tx, forms{k}, SamplesPerFrame = 4, NumBlocks = 1);
                ids = t.channels;
                valid = t.write(int16([1 2; 3 4; 5 6; 7 8]) * k);
                t.close();
                tc.verifyEqual(valid, true, sprintf('transmit, form %d', k));
                tc.verifyEqual(ids, {'voltage0_i', 'voltage0_q'}, sprintf('transmit, form %d', k));
                tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16((1:8).') * k, sprintf('transmit, form %d', k));
            end
        end

        function missingString(tc)
            % string(missing) passes a "(1,1) string" declaration; it has no text, and
            % nothing may take it for a name
            m = string(missing);
            verifyMatlabOrIio(tc, @() iio.Context(m));
            tc.verifyEqual(iio.internal.handle_count(), 0);
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            rx = ctx.find_device("emu-rx");
            a = phy.find_attr("text");
            ch = phy.find_channel("voltage0");
            n = iio.internal.handle_count();
            verifyMatlabOrIio(tc, @() ctx.find_device(m));
            verifyMatlabOrIio(tc, @() ctx.find_attr(m));
            verifyMatlabOrIio(tc, @() phy.find_channel(m));
            verifyMatlabOrIio(tc, @() phy.find_attr(m));
            verifyMatlabOrIio(tc, @() phy.find_debug_attr(m));
            verifyMatlabOrIio(tc, @() ch.find_attr(m));
            verifyMatlabOrIio(tc, @() a.write(m));
            verifyMatlabOrIio(tc, @() a.write_string(m));
            verifyMatlabOrIio(tc, @() iio.RxStream(rx, m, SamplesPerFrame = 8));
            verifyMatlabOrIio(tc, @() iio.RxStream(rx, ["voltage0_i" m], SamplesPerFrame = 8));
            tc.verifyEqual(a.read(), 'hello world');
            tc.verifyEqual(iio.internal.handle_count(), n);
        end

        function emptyAndNonAscii(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            mu = char(181);                                 % the micro sign
            names = {"", '', string(mu), mu, [mu 'V'], string([mu 'V']), " ", "emu-phy "};
            for k = 1:numel(names)
                d = ctx.find_device(names{k});
                tc.verifyEmpty(d, sprintf('name %d', k));
                tc.verifyClass(d, 'iio.Device');
                tc.verifySize(d, [1 0]);
            end
            names = {"", '', string(mu), mu};
            for k = 1:numel(names)
                a = phy.find_attr(names{k});
                tc.verifyClass(a, 'iio.Attr');
                tc.verifySize(a, [1 0], sprintf('attribute name %d', k));
                c = phy.find_channel(names{k});
                tc.verifyClass(c, 'iio.Channel');
                tc.verifySize(c, [1 0], sprintf('channel name %d', k));
                e = ctx.find_attr(names{k});
                tc.verifyClass(e, 'iio.Attr');
                tc.verifySize(e, [1 0], sprintf('context attribute name %d', k));
            end
            % a non-ASCII value survives the way to the device and back
            a = phy.find_attr("text");
            a.write(string([mu 'V']));
            tc.verifyEqual(a.read(), [mu 'V']);
            tc.verifyEqual(a.read_raw(), uint8([194 181 86 0]));   % UTF-8, and the zero that ends a value
            a.write([mu 'A']);
            tc.verifyEqual(a.read(), [mu 'A']);
        end

        function embeddedNul(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            rx = ctx.find_device("emu-rx");
            a = phy.find_attr("text");
            z = char(0);
            % C would cut each of these at the zero and find, or write, something else
            tc.verifyError(@() ctx.find_device(['emu-phy' z 'x']), 'iio:EINVAL');
            tc.verifyError(@() ctx.find_device(string(['emu-phy' z 'x'])), 'iio:EINVAL');
            tc.verifyError(@() ctx.find_device(['emu-phy' z]), 'iio:EINVAL');
            tc.verifyError(@() ctx.find_attr(['hw_model' z]), 'iio:EINVAL');
            tc.verifyError(@() phy.find_attr(['calib_mode' z]), 'iio:EINVAL');
            tc.verifyError(@() phy.find_attr(string(['calib_mode' z])), 'iio:EINVAL');
            tc.verifyError(@() phy.find_channel(['voltage0' z]), 'iio:EINVAL');
            tc.verifyError(@() a.write_string(['a' z 'b']), 'iio:EINVAL');
            tc.verifyError(@() a.write(string(['a' z 'b'])), 'iio:EINVAL');
            tc.verifyError(@() a.write(z), 'iio:EINVAL');
            tc.verifyEqual(a.read(), 'hello world');
            n = iio.internal.handle_count();
            tc.verifyError(@() iio.RxStream(rx, {['voltage0_i' z], 'voltage0_q'}, SamplesPerFrame = 8), 'iio:EINVAL');
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i" string(['voltage0_q' z 'x'])], SamplesPerFrame = 8), 'iio:EINVAL');
            tc.verifyError(@() iio.Context([tc.Fx.Uri z]), 'iio:EINVAL');
            tc.verifyEqual(iio.internal.handle_count(), n);
            % bytes, zero included, go through write_raw
            tc.verifyEqual(a.write_raw(uint8([97 0 98])), 3);
        end
    end
end

function verifyMatlabOrIio(tc, fcn)
%VERIFYMATLABORIIO FCN must throw, with an identifier of MATLAB or of the gateway.
id = '';
try
    fcn();
catch err
    id = err.identifier;
end
tc.verifyTrue(startsWith(id, 'MATLAB:') || startsWith(id, 'iio:'), ['identifier: ' id]);
end
