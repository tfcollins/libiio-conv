classdef MiniTx < handle
    %MINITX A transmitter that uses the iio package the way a toolbox transmitter does.
    %   Test support for tToolboxFlow: a plain handle class (no matlab.System) that
    %   performs the steps of a toolbox transmit object in the same order.
    %
    %   TX = MiniTx(URI, Name = Value) only stores its settings; every option is also a
    %   property that can be changed while the object is released:
    %       Device            name of the device (default "emu-tx")
    %       Channels          channel ids, I then Q of each column (default voltage0_i,
    %                         voltage0_q)
    %       SamplesPerFrame   largest number of rows of a frame (default 256)
    %       Cyclic            true: the first frame repeats (default false)
    %
    %   TX.setup() creates the context, sets its timeout, looks up the device, writes
    %   the frequency of its tone channel (followed by a read and a comparison) and
    %   opens the stream, which is always complex. A setup that fails releases what it
    %   built.
    %   VALID = TX.step(DATA) converts and queues one frame.
    %   TX.flush() waits until the queued frames are sent.
    %   TX.release() sends the frames in flight, closes the stream and destroys the
    %   context. It may be called any number of times, also on an object that was
    %   never set up.
    %
    %   See also MiniRx, iio.TxStream.

    properties
        Uri (1,1) string = ""
        Device (1,1) string = "emu-tx"
        Channels (1,:) string = ["voltage0_i", "voltage0_q"]
        SamplesPerFrame (1,1) double = 256
        Cyclic (1,1) logical = false
        NumBlocks (1,1) double = 2
        Timeout (1,1) double = 1000                 % ms, of the whole context
        ToneFrequency (1,1) int64 = int64(2000000)  % Hz: altvoltage0 (output) / frequency
    end

    properties (Access = private)
        Ctx = []        % iio.Context, [] while released
        Dev = []        % iio.Device
        Stream = []     % iio.TxStream
    end

    methods
        function obj = MiniTx(uri, opts)
            arguments
                uri (1,1) string
                opts.Device (1,1) string = "emu-tx"
                opts.Channels (1,:) string = ["voltage0_i", "voltage0_q"]
                opts.SamplesPerFrame (1,1) double = 256
                opts.Cyclic (1,1) logical = false
            end
            obj.Uri = uri;
            obj.Device = opts.Device;
            obj.Channels = opts.Channels;
            obj.SamplesPerFrame = opts.SamplesPerFrame;
            obj.Cyclic = opts.Cyclic;
        end

        function setup(obj)
            %SETUP Context, timeout, device, attribute, stream: in this order.
            obj.release();
            try
                obj.Ctx = iio.Context(obj.Uri);
                obj.Ctx.set_timeout(obj.Timeout);
                obj.Dev = obj.Ctx.find_device(obj.Device);
                if isempty(obj.Dev)
                    error('MiniTx:noDevice', 'The context has no device "%s".', obj.Device);
                end
                obj.setToneFrequency();
                obj.Stream = iio.TxStream(obj.Dev, obj.Channels, ...
                    SamplesPerFrame = obj.SamplesPerFrame, Complex = true, ...
                    NumBlocks = obj.NumBlocks, Cyclic = obj.Cyclic);
            catch err
                obj.release();
                rethrow(err);
            end
        end

        function valid = step(obj, data)
            %STEP Queue one frame: 1 to SamplesPerFrame rows, one column per I/Q pair.
            if isempty(obj.Stream)
                error('MiniTx:notSetUp', 'Call setup before step.');
            end
            valid = obj.Stream.write(data);
        end

        function flush(obj)
            %FLUSH Wait until the queued frames are sent.
            if isempty(obj.Stream)
                error('MiniTx:notSetUp', 'Call setup before flush.');
            end
            if ~obj.Stream.flush()
                error('MiniTx:flush', 'The queued frames were not sent in time.');
            end
        end

        function release(obj)
            %RELEASE Send the frames in flight, close the stream, destroy the context.
            if ~isempty(obj.Stream)
                obj.Stream.close();
            end
            obj.Stream = [];
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
        function setToneFrequency(obj)
            ch = obj.Dev.find_channel("altvoltage0", true);
            if isempty(ch)
                error('MiniTx:noChannel', 'Device "%s" has no output channel "altvoltage0".', obj.Device);
            end
            a = ch.find_attr("frequency");
            if isempty(a)
                error('MiniTx:noAttribute', 'Channel "altvoltage0" has no attribute "frequency".');
            end
            a.write_longlong(obj.ToneFrequency);
            got = a.read_longlong();
            if got ~= obj.ToneFrequency
                error('MiniTx:readBack', 'Tone frequency: wrote %d, read %d.', obj.ToneFrequency, got);
            end
        end
    end
end
