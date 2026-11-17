classdef MiniRx < handle
    %MINIRX A receiver that uses the iio package the way a toolbox receiver does.
    %   Test support for tToolboxFlow: a plain handle class (no matlab.System) that
    %   performs the steps of a toolbox receive object in the same order.
    %
    %   RX = MiniRx(URI, Name = Value) only stores its settings; every option is also a
    %   property that can be changed while the object is released:
    %       Device            name of the streaming device (default "emu-rx")
    %       Phy               name of the control device, "" for none (default "emu-phy")
    %       Channels          channel ids in column order (default voltage0_i, voltage0_q)
    %       SamplesPerFrame   rows of a frame (default 1024)
    %       OutputDataType, Complex, Raw, Scale
    %                         as for iio.RxStream (default "int16", true, false, "none")
    %
    %   RX.setup() creates the context, sets its timeout, looks up the devices, writes
    %   the attributes of the control device (each write is followed by a read and a
    %   comparison) and opens the stream. A setup that fails releases what it built.
    %   [DATA, VALID] = RX.step() returns the next frame.
    %   RX.release() closes the stream and destroys the context. It may be called any
    %   number of times, also on an object that was never set up.
    %
    %   See also MiniTx, iio.RxStream.

    properties
        Uri (1,1) string = ""
        Device (1,1) string = "emu-rx"
        Phy (1,1) string = "emu-phy"
        Channels (1,:) string = ["voltage0_i", "voltage0_q"]
        SamplesPerFrame (1,1) double = 1024
        OutputDataType (1,1) string = "int16"
        Complex (1,1) logical = true
        Raw (1,1) logical = false
        Scale (1,1) string = "none"
        Timeout (1,1) double = 1000                     % ms, of the whole context
        CenterFrequency (1,1) int64 = int64(2450000000) % Hz: altvoltage0 (output) / frequency
        RFBandwidth (1,1) int64 = int64(20000000)       % Hz: voltage0 (input) / rf_bandwidth
        GainControlMode (1,1) string = "manual"         % voltage0 (input) / gain_control_mode
    end

    properties (Access = private)
        Ctx = []        % iio.Context, [] while released
        Dev = []        % iio.Device: the streaming device
        PhyDev = []     % iio.Device: the control device
        Stream = []     % iio.RxStream
    end

    methods
        function obj = MiniRx(uri, opts)
            arguments
                uri (1,1) string
                opts.Device (1,1) string = "emu-rx"
                opts.Phy (1,1) string = "emu-phy"
                opts.Channels (1,:) string = ["voltage0_i", "voltage0_q"]
                opts.SamplesPerFrame (1,1) double = 1024
                opts.OutputDataType (1,1) string = "int16"
                opts.Complex (1,1) logical = true
                opts.Raw (1,1) logical = false
                opts.Scale (1,1) string = "none"
            end
            obj.Uri = uri;
            obj.Device = opts.Device;
            obj.Phy = opts.Phy;
            obj.Channels = opts.Channels;
            obj.SamplesPerFrame = opts.SamplesPerFrame;
            obj.OutputDataType = opts.OutputDataType;
            obj.Complex = opts.Complex;
            obj.Raw = opts.Raw;
            obj.Scale = opts.Scale;
        end

        function setup(obj)
            %SETUP Context, timeout, devices, attributes, stream: in this order.
            obj.release();
            try
                obj.Ctx = iio.Context(obj.Uri);
                obj.Ctx.set_timeout(obj.Timeout);
                obj.Dev = obj.findDevice(obj.Device);
                if obj.Phy ~= ""
                    % the control device is not the streaming device
                    obj.PhyDev = obj.findDevice(obj.Phy);
                    obj.setLongLong("voltage0", false, "rf_bandwidth", obj.RFBandwidth);
                    obj.setString("voltage0", false, "gain_control_mode", obj.GainControlMode);
                    obj.setLongLong("altvoltage0", true, "frequency", obj.CenterFrequency);
                end
                obj.Stream = iio.RxStream(obj.Dev, obj.Channels, ...
                    SamplesPerFrame = obj.SamplesPerFrame, OutputDataType = obj.OutputDataType, ...
                    Complex = obj.Complex, Raw = obj.Raw, Scale = obj.Scale);
            catch err
                obj.release();
                rethrow(err);
            end
        end

        function [data, valid] = step(obj)
            %STEP The next frame: SamplesPerFrame rows, VALID false when none arrived in time.
            if isempty(obj.Stream)
                error('MiniRx:notSetUp', 'Call setup before step.');
            end
            [data, valid] = obj.Stream.read();
        end

        function release(obj)
            %RELEASE Close the stream and destroy the context; harmless when repeated.
            if ~isempty(obj.Stream)
                obj.Stream.close();
            end
            obj.Stream = [];
            obj.PhyDev = [];
            obj.Dev = [];
            if ~isempty(obj.Ctx)
                delete(obj.Ctx);
            end
            obj.Ctx = [];
        end

        function delete(obj)
            obj.release();
        end
    end

    methods (Access = private)
        function dev = findDevice(obj, name)
            dev = obj.Ctx.find_device(name);
            if isempty(dev)
                error('MiniRx:noDevice', 'The context has no device "%s".', name);
            end
        end

        function a = findAttr(obj, id, isOutput, name)
            ch = obj.PhyDev.find_channel(id, isOutput);
            if isempty(ch)
                error('MiniRx:noChannel', 'The control device has no channel "%s".', id);
            end
            a = ch.find_attr(name);
            if isempty(a)
                error('MiniRx:noAttribute', 'Channel "%s" has no attribute "%s".', id, name);
            end
        end

        function setLongLong(obj, id, isOutput, name, value)
            a = obj.findAttr(id, isOutput, name);
            a.write_longlong(value);
            got = a.read_longlong();
            if got ~= value
                error('MiniRx:readBack', 'Attribute "%s" of "%s": wrote %d, read %d.', name, id, value, got);
            end
        end

        function setString(obj, id, isOutput, name, value)
            a = obj.findAttr(id, isOutput, name);
            a.write_string(value);
            got = a.read();
            if ~strcmp(got, char(value))
                error('MiniRx:readBack', 'Attribute "%s" of "%s": wrote "%s", read "%s".', name, id, value, got);
            end
        end
    end
end
