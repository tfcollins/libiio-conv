classdef Device < iio.internal.Object
    %DEVICE An IIO device of a context.
    %   Device objects are returned by the devices property and the find_device method
    %   of an iio.Context; they are not created directly. A Device keeps its context
    %   alive.
    %
    %   channels, attrs and buffers list what the device has, in libiio's order; the
    %   position of a channel in channels is its bit in a channels mask. find_channel
    %   and find_attr look one up by name.
    %
    %   See also iio.Context, iio.Channel, iio.Attr, iio.Buffer.

    properties (SetAccess = immutable)
        id char
        name char       % '' when the device has none
        label char      % '' when the device has none
    end

    properties (Dependent)
        context         % iio.Context the device belongs to
        is_trigger      % logical: the device is a trigger
        is_hwmon        % logical: the device is a hardware-monitoring device
        channels        % 1-by-N iio.Channel, libiio order (position k is channels-mask bit k-1)
        attrs           % 1-by-N iio.Attr: the device attributes
        buffers         % 1-by-N iio.Buffer
        debug_attrs     % 1-by-N iio.Attr: the debug attributes
        event_attrs     % 1-by-N iio.Attr: the event attributes
    end

    methods
        function obj = Device(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
            if handle ~= 0
                obj.id = iio_mex('device_get_id', handle);
                obj.name = char(iio_mex('device_get_name', handle));     % [] -> ''
                obj.label = char(iio_mex('device_get_label', handle));
            end
        end

        function ctx = get.context(obj)
            h = iio_mex('device_get_context', obj.Handle);
            ctx = obj.Parent;
            if ~(isa(ctx, 'iio.Context') && isscalar(ctx) && isvalid(ctx) && ctx.Handle == h)
                error('iio:EBADF', 'The context of this device is no longer the one it was obtained from.');
            end
        end

        function tf = get.is_trigger(obj)
            tf = iio_mex('device_is_trigger', obj.Handle);
        end

        function tf = get.is_hwmon(obj)
            tf = iio_mex('device_is_hwmon', obj.Handle);
        end

        function ch = get.channels(obj)
            h = iio_mex('device_get_channels', obj.Handle);
            ch = iio.Channel.empty(1, 0);
            for k = 1:numel(h)
                ch(k) = iio.Channel(obj, h(k));
            end
        end

        function a = get.attrs(obj)
            a = obj.attrList(iio_mex('device_get_attrs', obj.Handle));
        end

        function b = get.buffers(obj)
            h = iio_mex('device_get_buffers', obj.Handle);
            b = iio.Buffer.empty(1, 0);
            for k = 1:numel(h)
                b(k) = iio.Buffer(obj, h(k));
            end
        end

        function a = get.debug_attrs(obj)
            a = obj.attrList(iio_mex('device_get_debug_attrs', obj.Handle));
        end

        function a = get.event_attrs(obj)
            a = obj.attrList(iio_mex('device_get_event_attrs', obj.Handle));
        end

        function ch = find_channel(obj, name, is_output)
            %FIND_CHANNEL Channel with that id, label or name: an iio.Channel, or a 1-by-0 iio.Channel.
            %   CH = FIND_CHANNEL(NAME) looks among the input channels,
            %   CH = FIND_CHANNEL(NAME, true) among the output channels.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                name (1,1) string
                is_output (1,1) logical = false
            end
            h = iio_mex('device_find_channel', obj.Handle, char(name), is_output);
            if h == 0
                ch = iio.Channel.empty(1, 0);
            else
                ch = iio.Channel(obj, h);
            end
        end

        function a = find_attr(obj, name)
            %FIND_ATTR Device attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                name (1,1) string
            end
            a = obj.attrList(iio_mex('device_find_attr', obj.Handle, char(name)));
        end

        function a = find_debug_attr(obj, name)
            %FIND_DEBUG_ATTR Debug attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                name (1,1) string
            end
            a = obj.attrList(iio_mex('device_find_debug_attr', obj.Handle, char(name)));
        end

        function a = find_event_attr(obj, name)
            %FIND_EVENT_ATTR Event attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                name (1,1) string
            end
            a = obj.attrList(iio_mex('device_find_event_attr', obj.Handle, char(name)));
        end

        function n = sample_size(obj, mask)
            %SAMPLE_SIZE Bytes of one sample of this device with the channels of MASK enabled.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
            end
            n = iio_mex('device_get_sample_size', obj.Handle, mask.Handle);
        end

        function t = get_trigger(obj)
            %GET_TRIGGER The trigger assigned to this device: an iio.Device, or a 1-by-0 iio.Device.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
            end
            h = iio_mex('device_get_trigger', obj.Handle);
            if h == 0
                t = iio.Device.empty(1, 0);
            else
                t = iio.Device(obj.Parent, h);
            end
        end

        function set_trigger(obj, trig)
            %SET_TRIGGER Assign the trigger device TRIG to this device; [] removes the trigger.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                trig
            end
            if isempty(trig)
                h = uint64(0);
            elseif isa(trig, 'iio.Device') && isscalar(trig)
                h = trig.Handle;
            else
                error('iio:EINVAL', 'A trigger is one iio.Device, or [] to remove the trigger.');
            end
            iio_mex('device_set_trigger', obj.Handle, h);
        end

        function v = reg_read(obj, addr)
            %REG_READ Value (uint32) of the hardware register at address ADDR.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                addr (1,1) {mustBeNumeric, mustBeReal}
            end
            v = iio_mex('device_reg_read', obj.Handle, addr);
        end

        function reg_write(obj, addr, value)
            %REG_WRITE Write VALUE to the hardware register at address ADDR.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
                addr (1,1) {mustBeNumeric, mustBeReal}
                value (1,1) {mustBeNumeric, mustBeReal}
            end
            iio_mex('device_reg_write', obj.Handle, addr, value);
        end

        function es = create_event_stream(obj)
            %CREATE_EVENT_STREAM An iio.EventStream that reads the events of this device.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Device")}
            end
            es = iio.EventStream(obj);
        end
    end

    methods (Access = private)
        function a = attrList(obj, h)
            %ATTRLIST 1-by-N iio.Attr of the ids H (an id 0 is left out), with this device as parent.
            a = iio.Attr.empty(1, 0);
            h = h(h ~= 0);
            for k = 1:numel(h)
                a(k) = iio.Attr(obj, h(k));
            end
        end
    end
end
