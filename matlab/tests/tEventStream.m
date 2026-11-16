classdef tEventStream < IioTestCase
    %TEVENTSTREAM iio.EventStream: decoding of event codes, reading on the fake backend, lifetime.
    %   emu has no event streams; the fake backend has one on emu-phy that delivers two
    %   events and then none.

    methods
        function verifyNoChannel(tc, ch)
            %VERIFYNOCHANNEL CH is a 1-by-0 iio.Channel.
            tc.verifyClass(ch, 'iio.Channel');
            tc.verifySize(ch, [1 0]);
            tc.verifyTrue(isempty(ch));
        end
    end

    methods (Test)
        function notSupportedOnEmu(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            tc.verifyError(@() iio.EventStream(phy), 'iio:ENOSYS');
            tc.verifyError(@() phy.create_event_stream(), 'iio:ENOSYS');
            tc.verifyIioError(@() iio.EventStream(phy), "ENOSYS");
        end

        function decode(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");

            % threshold, rising, voltage 0: the input channel
            ev = iio.EventStream.decode(0x0001000000000000u64, phy);
            tc.verifyClass(ev, 'struct');
            tc.verifySize(ev, [1 1]);
            tc.verifyEqual(fieldnames(ev), {'id'; 'type'; 'direction'; 'type_id'; ...
                'direction_id'; 'channel'; 'channel_diff'});
            tc.verifyClass(ev.id, 'uint64');
            tc.verifyEqual(ev.id, 0x0001000000000000u64);
            tc.verifyEqual(ev.type, 'IIO_EV_TYPE_THRESH');
            tc.verifyEqual(ev.type_id, 0);
            tc.verifyEqual(ev.direction, 'IIO_EV_DIR_RISING');
            tc.verifyEqual(ev.direction_id, 1);
            tc.verifyClass(ev.type_id, 'double');
            tc.verifyClass(ev.direction_id, 'double');
            tc.verifyClass(ev.channel, 'iio.Channel');
            tc.verifySize(ev.channel, [1 1]);
            tc.verifyEqual(ev.channel.id, 'voltage0');
            tc.verifyTrue(ev.channel == phy.find_channel("voltage0"));
            tc.verifyTrue(ev.channel ~= phy.find_channel("voltage0", true));
            tc.verifyNoChannel(ev.channel_diff);

            % magnitude, falling, acceleration with modifier x
            ev = iio.EventStream.decode(0x0102010300000000u64, phy);
            tc.verifyEqual(ev.id, 0x0102010300000000u64);
            tc.verifyEqual(ev.type, 'IIO_EV_TYPE_MAG');
            tc.verifyEqual(ev.type_id, 1);
            tc.verifyEqual(ev.direction, 'IIO_EV_DIR_FALLING');
            tc.verifyEqual(ev.direction_id, 2);
            tc.verifySize(ev.channel, [1 1]);
            tc.verifyEqual(ev.channel.id, 'accel_x');
            tc.verifyNoChannel(ev.channel_diff);

            % pressure: emu-phy has no such channel (libiio alone would answer accel_x)
            ev = iio.EventStream.decode(0x0000001100000000u64, phy);
            tc.verifyEqual(ev.type, 'IIO_EV_TYPE_THRESH');
            tc.verifyEqual(ev.type_id, 0);
            tc.verifyEqual(ev.direction, 'IIO_EV_DIR_EITHER');
            tc.verifyEqual(ev.direction_id, 0);
            tc.verifyTrue(isempty(ev.channel));
            tc.verifyNoChannel(ev.channel);
            tc.verifyNoChannel(ev.channel_diff);

            % differential: altvoltage 0 against altvoltage 1
            ev = iio.EventStream.decode(0x0080000f00010000u64, phy);
            tc.verifyEqual(ev.type, 'IIO_EV_TYPE_THRESH');
            tc.verifyEqual(ev.direction, 'IIO_EV_DIR_EITHER');
            tc.verifyEqual(ev.direction_id, 0);
            tc.verifySize(ev.channel, [1 1]);
            tc.verifySize(ev.channel_diff, [1 1]);
            tc.verifyEqual(ev.channel.id, 'altvoltage0');
            tc.verifyEqual(ev.channel_diff.id, 'altvoltage1');
            tc.verifyTrue(ev.channel == phy.find_channel("altvoltage0", true));
            tc.verifyTrue(ev.channel_diff == phy.find_channel("altvoltage1", true));
        end

        function decodeArguments(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            % a type without a name
            ev = iio.EventStream.decode(0xC800000000000000u64, phy);
            tc.verifyEqual(ev.type, 'UNKNOWN');
            tc.verifyEqual(ev.type_id, 200);
            tc.verifyEqual(ev.channel.id, 'voltage0');
            % not a device: refused by the argument validation, or by the gateway when
            % MATLAB converts the value to a device without a handle
            id = '';
            try
                iio.EventStream.decode(0x0001000000000000u64, 5);
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:') || startsWith(id, 'iio:'), id);
            % a device of a context that is gone
            delete(ctx);
            tc.verifyError(@() iio.EventStream.decode(0x0001000000000000u64, phy), 'iio:EBADF');
        end

        function readFake(tc)
            ctx = tc.Fx.fakeContext();
            phy = ctx.find_device("emu-phy");
            es = iio.EventStream(phy);
            tc.verifyClass(es, 'iio.EventStream');
            tc.verifySize(es, [1 1]);
            tc.verifyClass(es.device, 'iio.Device');
            tc.verifyTrue(es.device == phy);

            ev = es.read();
            tc.verifyClass(ev, 'struct');
            tc.verifySize(ev, [1 1]);
            tc.verifyEqual(fieldnames(ev), {'id'; 'type'; 'direction'; 'type_id'; ...
                'direction_id'; 'channel'; 'channel_diff'; 'timestamp'});
            tc.verifyEqual(class(ev.id), 'uint64');
            tc.verifyEqual(ev.id, 0x0001000000000000u64);
            tc.verifyEqual(class(ev.timestamp), 'int64');
            tc.verifyEqual(ev.timestamp, int64(1000));
            tc.verifyEqual(ev.type, 'IIO_EV_TYPE_THRESH');
            tc.verifyEqual(ev.type_id, 0);
            tc.verifyEqual(ev.direction, 'IIO_EV_DIR_RISING');
            tc.verifyEqual(ev.direction_id, 1);
            tc.verifySize(ev.channel, [1 1]);
            tc.verifyEqual(ev.channel.id, 'voltage0');
            tc.verifyTrue(ev.channel == phy.find_channel("voltage0"));
            tc.verifyNoChannel(ev.channel_diff);

            ev = es.read(true);
            tc.verifyEqual(ev.id, 0x0102010300000000u64);
            tc.verifyEqual(class(ev.id), 'uint64');
            tc.verifyEqual(ev.timestamp, int64(1001));
            tc.verifyEqual(class(ev.timestamp), 'int64');
            tc.verifyEqual(ev.type, 'IIO_EV_TYPE_MAG');
            tc.verifyEqual(ev.type_id, 1);
            tc.verifyEqual(ev.direction, 'IIO_EV_DIR_FALLING');
            tc.verifyEqual(ev.direction_id, 2);
            tc.verifyEqual(ev.channel.id, 'accel_x');
            tc.verifyNoChannel(ev.channel_diff);

            % no third event: [] from a read that does not wait, and from one that waits
            % (the fake backend reports a timeout at once)
            none = es.read();
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'double');
            tc.verifySize(none, [0 0]);
            tc.verifyTrue(isempty(es.read(false)));

            % every stream starts with the first event
            es2 = phy.create_event_stream();
            tc.verifyClass(es2, 'iio.EventStream');
            tc.verifyTrue(es2 ~= es);
            ev = es2.read();
            tc.verifyEqual(ev.id, 0x0001000000000000u64);
            tc.verifyEqual(ev.timestamp, int64(1000));
        end

        function wrongDeviceFake(tc)
            ctx = tc.Fx.fakeContext();
            rx = ctx.find_device("emu-rx");
            n = iio.internal.handle_count();
            tc.verifyError(@() iio.EventStream(rx), 'iio:ENODEV');
            tc.verifyError(@() rx.create_event_stream(), 'iio:ENODEV');
            tc.verifyEqual(iio.internal.handle_count(), n);
        end

        function constructorArguments(tc)
            tc.verifyError(@() iio.EventStream(5), 'iio:EINVAL');
            tc.verifyError(@() iio.EventStream(iio.Device.empty(1, 0)), 'iio:EINVAL');
            % the form without arguments holds no handle and calls no gateway command
            tc.verifyEqual(iio.internal.Object.handles(iio.EventStream()), uint64(0));
            e = iio.EventStream.empty(1, 0);
            tc.verifySize(e, [1 0]);
            tc.verifyClass(e, 'iio.EventStream');
        end

        function destroyTwice(tc)
            ctx = tc.Fx.fakeContext();
            phy = ctx.find_device("emu-phy");
            es = iio.EventStream(phy);
            n = iio.internal.handle_count();
            es.destroy();
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
            es.destroy();
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
            tc.verifyTrue(isvalid(es));
            tc.verifyError(@() es.read(), 'iio:EBADF');
            delete(es);
            tc.verifyFalse(isvalid(es));
            tc.verifyEqual(iio.internal.handle_count(), n - 1);
        end

        function outlivesContext(tc)
            ctx = tc.Fx.fakeContext();
            phy = ctx.find_device("emu-phy");
            es = iio.EventStream(phy);
            ev = es.read();
            tc.verifyEqual(ev.timestamp, int64(1000));
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyError(@() es.read(), 'iio:EBADF');
            es.destroy();       % a stale handle: nothing happens
            delete(es);
        end
    end
end
