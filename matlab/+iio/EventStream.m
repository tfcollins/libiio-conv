classdef EventStream < iio.internal.Object
    %EVENTSTREAM The stream of events of an IIO device (thresholds crossed, faults, ...).
    %   ES = iio.EventStream(DEV) opens the event stream of the iio.Device DEV; so does
    %   DEV.create_event_stream(). A backend or a device without events raises an
    %   error (iio:ENOSYS, iio:ENODEV). The stream is closed when ES is deleted or
    %   goes out of scope, by destroy, and when its context is destroyed. An
    %   EventStream keeps its device, and with it the context, alive.
    %
    %   EV = ES.read() returns the next event as a struct, or [] when there is none:
    %       id            uint64: the event code of the kernel
    %       type          char such as 'IIO_EV_TYPE_THRESH'; 'UNKNOWN' for a value without a name
    %       direction     char such as 'IIO_EV_DIR_RISING'
    %       type_id       double: the value of the event type
    %       direction_id  double: the value of the direction
    %       channel       iio.Channel the event is about, or a 1-by-0 iio.Channel
    %       channel_diff  second channel of a differential event, or a 1-by-0 iio.Channel
    %       timestamp     int64: nanoseconds, as the kernel reports them
    %   ES.read(false) waits for an event. On a real backend that wait has no time
    %   limit and cannot be interrupted, so the default is not to wait.
    %
    %   EV = iio.EventStream.decode(ID, DEV) gives the same struct, without timestamp,
    %   for the event code ID seen on the device DEV.
    %
    %   See also iio.Device, iio.Channel.

    properties (Dependent)
        device          % iio.Device whose events this stream reads
    end

    methods
        function obj = EventStream(dev, handle)
            %EVENTSTREAM ES = iio.EventStream(DEV), DEV one iio.Device.
            %   The forms EventStream() and EventStream(PARENT, HANDLE) are internal.
            arguments
                dev = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(dev, handle);
            if nargin == 1
                if ~(isa(dev, 'iio.Device') && isscalar(dev))
                    error('iio:EINVAL', 'An event stream is opened on one iio.Device.');
                end
                obj.Handle = iio_mex('device_create_event_stream', dev.Handle);
            end
        end

        function delete(obj)
            %DELETE Close the event stream.
            if obj.Handle ~= 0
                iio_mex('event_stream_destroy', obj.Handle);
            end
        end

        function destroy(obj)
            %DESTROY Close the event stream now. A later read raises iio:EBADF.
            arguments
                obj (1,1) {mustBeA(obj, "iio.EventStream")}
            end
            if obj.Handle ~= 0
                iio_mex('event_stream_destroy', obj.Handle);
            end
        end

        function d = get.device(obj)
            d = obj.Parent;
        end

        function ev = read(obj, nonblock)
            %READ The next event as a struct (see iio.EventStream), or [] when there is none.
            %   EV = READ() returns at once; EV = READ(false) waits for an event. A
            %   backend that reports a timeout while waiting gives [] as well.
            arguments
                obj (1,1) {mustBeA(obj, "iio.EventStream")}
                nonblock (1,1) logical = true
            end
            [tf, id, timestamp] = iio_mex('event_stream_read', obj.Handle, nonblock);
            if tf
                ev = iio.EventStream.decode(id, obj.Parent);
                ev.timestamp = timestamp;
            else
                ev = [];
            end
        end
    end

    methods (Static)
        function ev = decode(id, dev)
            %DECODE Struct that describes the event code ID (uint64) of the iio.Device DEV.
            %   Fields: id, type, direction, type_id, direction_id, channel, channel_diff
            %   (see iio.EventStream). channel and channel_diff are 1-by-0 iio.Channel
            %   arrays when DEV has no channel that the event names.
            arguments
                id (1,1) uint64
                dev (1,1) {mustBeA(dev, "iio.Device")}
            end
            [type, type_id] = iio_mex('event_get_type', id);
            [direction, direction_id] = iio_mex('event_get_direction', id);
            h = iio_mex('event_get_channel', id, dev.Handle, false);
            hdiff = iio_mex('event_get_channel', id, dev.Handle, true);
            ev = struct('id', id, 'type', type, 'direction', direction, ...
                'type_id', type_id, 'direction_id', direction_id);
            ev.channel = iio.Channel.empty(1, 0);
            ev.channel_diff = iio.Channel.empty(1, 0);
            if h ~= 0
                ev.channel = iio.Channel(dev, h);
            end
            if hdiff ~= 0
                ev.channel_diff = iio.Channel(dev, hdiff);
            end
        end
    end
end
