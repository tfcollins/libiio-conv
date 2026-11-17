classdef Channel < iio.internal.Object
    %CHANNEL A channel of an IIO device.
    %   Channel objects are returned by the channels property and the find_channel
    %   method of an iio.Device (and by the scan_elements of an iio.Buffer); they are
    %   not created directly. A Channel keeps its device, and with it the context,
    %   alive.
    %
    %   id, name, label, output, scan_element and index never change and are read
    %   once. type and modifier name the kind of channel ('IIO_VOLTAGE', 'IIO_MOD_I');
    %   attrs and find_attr give its attributes; data_format describes how a sample
    %   is stored in a buffer.
    %
    %   A channel is selected for streaming by enabling it in an iio.ChannelsMask:
    %   enable, disable, is_enabled. read and write move the samples of this channel
    %   out of and into an iio.Block; convert and convert_inverse translate single
    %   samples between the hardware format and the host format.
    %
    %   See also iio.Device, iio.ChannelsMask, iio.Attr, iio.Block.

    properties (SetAccess = immutable)
        id char
        name char                       % '' when the channel has none
        label char                      % '' when the channel has none
        output (1,1) logical = false    % true for an output channel
        scan_element (1,1) logical = false   % true when the channel can be streamed through a buffer
        index (1,1) double = 0          % scan index; negative when the channel is not a scan element
    end

    properties (Dependent)
        device          % iio.Device the channel belongs to
        type            % char: 'IIO_VOLTAGE', ...; 'HWMON_TEMP', ... on a hardware-monitoring device
        type_id         % double: the number behind type
        modifier        % char: 'IIO_MOD_I', 'IIO_NO_MOD', ...
        modifier_id     % double: the number behind modifier
        attrs           % 1-by-N iio.Attr: the channel attributes
        event_attrs     % 1-by-N iio.Attr: the event attributes of the channel
        data_format     % struct: length, bits, shift, repeat, is_signed, is_fully_defined, is_be, with_scale, is_float, scale, offset
    end

    methods
        function obj = Channel(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
            if handle ~= 0
                obj.id = iio_mex('channel_get_id', handle);
                obj.name = char(iio_mex('channel_get_name', handle));     % [] -> ''
                obj.label = char(iio_mex('channel_get_label', handle));
                obj.output = iio_mex('channel_is_output', handle);
                obj.scan_element = iio_mex('channel_is_scan_element', handle);
                obj.index = iio_mex('channel_get_index', handle);
            end
        end

        function dev = get.device(obj)
            h = iio_mex('channel_get_device', obj.Handle);
            dev = obj.Parent;
            if ~(isa(dev, 'iio.Device') && isscalar(dev) && isvalid(dev) && dev.Handle == h)
                error('iio:EBADF', 'The device of this channel is no longer the one it was obtained from.');
            end
        end

        function s = get.type(obj)
            s = obj.typeOf();
        end

        function v = get.type_id(obj)
            [~, v] = obj.typeOf();
        end

        function s = get.modifier(obj)
            s = iio_mex('channel_get_modifier', obj.Handle);
        end

        function v = get.modifier_id(obj)
            [~, v] = iio_mex('channel_get_modifier', obj.Handle);
        end

        function a = get.attrs(obj)
            a = obj.attrList(iio_mex('channel_get_attrs', obj.Handle));
        end

        function a = get.event_attrs(obj)
            a = obj.attrList(iio_mex('channel_get_event_attrs', obj.Handle));
        end

        function f = get.data_format(obj)
            f = iio_mex('channel_get_data_format', obj.Handle);
        end

        function a = find_attr(obj, name)
            %FIND_ATTR Channel attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                name (1,1) string
            end
            a = obj.attrList(iio_mex('channel_find_attr', obj.Handle, char(name)));
        end

        function a = find_event_attr(obj, name)
            %FIND_EVENT_ATTR Event attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                name (1,1) string
            end
            a = obj.attrList(iio_mex('channel_find_event_attr', obj.Handle, char(name)));
        end

        function enable(obj, mask)
            %ENABLE Enable the channel, or every channel of a row of channels, in MASK.
            %   Only scan-element channels of the device of MASK can be enabled
            %   (iio:EINVAL otherwise). The mask of a buffer stream is read-only
            %   (iio:EPERM). An empty row of channels, such as find_channel returns for
            %   an id the device does not have, is iio:EINVAL.
            arguments
                obj (1,:) {mustBeA(obj, "iio.Channel")}
                mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
            end
            if isempty(obj)
                error('iio:EINVAL', 'No channel to enable (did find_channel find nothing?).');
            end
            for k = 1:numel(obj)
                iio_mex('channel_enable', obj(k).Handle, mask.Handle);
            end
        end

        function disable(obj, mask)
            %DISABLE Disable the channel, or every channel of a row of channels, in MASK.
            %   An empty row of channels is iio:EINVAL, as for enable.
            arguments
                obj (1,:) {mustBeA(obj, "iio.Channel")}
                mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
            end
            if isempty(obj)
                error('iio:EINVAL', 'No channel to disable (did find_channel find nothing?).');
            end
            for k = 1:numel(obj)
                iio_mex('channel_disable', obj(k).Handle, mask.Handle);
            end
        end

        function tf = is_enabled(obj, mask)
            %IS_ENABLED True when the channel is enabled in MASK (a mask of the same device).
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
            end
            tf = iio_mex('channel_is_enabled', obj.Handle, mask.Handle);
        end

        function refresh_format(obj)
            %REFRESH_FORMAT Ask the backend for the data format of the channel again.
            %   Raises iio:ENOSYS on a backend that cannot do it.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
            end
            iio_mex('channel_refresh_format', obj.Handle);
        end

        function data = read(obj, block, raw)
            %READ Samples of this channel in BLOCK, as a column.
            %   DATA = READ(BLOCK) converts every sample to the host format (byte order,
            %   shift, sign extension) and returns intN or uintN values, N being the
            %   storage length of the data format and the sign its is_signed. When the
            %   format repeats, is floating point, or has a length other than 8, 16, 32
            %   or 64 bits, the converted bytes are returned as a uint8 column.
            %   DATA = READ(BLOCK, true) returns the bytes as they are in the block
            %   (uint8 column).
            %   A channel that is not enabled in the mask of the block's stream gives an
            %   empty (0-by-1) result of the same class.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                block (1,1) {mustBeA(block, "iio.Block")}
                raw (1,1) logical = false
            end
            b = iio_mex('channel_read', obj.Handle, block.Handle, raw);
            data = b(:);
            if ~raw
                f = iio_mex('channel_get_data_format', obj.Handle);
                if f.repeat == 1 && ~f.is_float && any(f.length == [8 16 32 64])
                    if f.is_signed
                        cls = sprintf('int%d', f.length);
                    else
                        cls = sprintf('uint%d', f.length);
                    end
                    if isempty(data)
                        data = zeros(0, 1, cls);
                    else
                        data = typecast(data, cls);
                        data = data(:);
                    end
                end
            end
        end

        function n = write(obj, block, data, raw)
            %WRITE Write samples of this channel into BLOCK; N is the number of bytes used.
            %   N = WRITE(BLOCK, DATA) takes DATA (a numeric vector) as samples in the
            %   host format, in the bytes of its own class: typecast(DATA(:), 'uint8'), so
            %   an int16 vector for a channel stored in 16 bits. The samples are
            %   converted to the hardware format.
            %   N = WRITE(BLOCK, DATA, true) copies the bytes unchanged.
            %   Only whole samples are written: N is a multiple of the bytes of one sample
            %   of the channel, at most what the block holds, and 0 for a channel that is
            %   not enabled in the mask of the block's stream. No Gain, Offset or scale is
            %   applied here: DATA holds the values the device gets (iio.Block.write and
            %   iio.TxStream convert).
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                block (1,1) {mustBeA(block, "iio.Block")}
                data {mustBeNumeric, mustBeReal}
                raw (1,1) logical = false
            end
            if isempty(data)
                bytes = zeros(0, 1, 'uint8');
            else
                bytes = typecast(full(data(:)), 'uint8');
            end
            n = iio_mex('channel_write', obj.Handle, block.Handle, bytes, raw);
        end

        function out = convert(obj, raw_bytes)
            %CONVERT Samples from the hardware format to the host format.
            %   OUT = CONVERT(RAW_BYTES) takes whole samples as uint8 (a multiple of
            %   data_format.length/8 * data_format.repeat bytes) and returns a uint8
            %   column of the same length. A channel that is not a scan element has no
            %   sample format: iio:EINVAL.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                raw_bytes (1,:) uint8
            end
            out = iio_mex('channel_convert', obj.Handle, raw_bytes).';
        end

        function out = convert_inverse(obj, host_bytes)
            %CONVERT_INVERSE Samples from the host format to the hardware format.
            %   OUT = CONVERT_INVERSE(HOST_BYTES): uint8 in, uint8 column out, as convert.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Channel")}
                host_bytes (1,:) uint8
            end
            out = iio_mex('channel_convert_inverse', obj.Handle, host_bytes).';
        end
    end

    methods (Access = private)
        function [s, v] = typeOf(obj)
            %TYPEOF Name and number of the channel type, from the table of the device's kind.
            %   A hardware-monitoring device numbers its channel types with enum
            %   hwmon_chan_type: the same number means something else there.
            if iio_mex('device_is_hwmon', iio_mex('channel_get_device', obj.Handle))
                [s, v] = iio_mex('hwmon_channel_get_type', obj.Handle);
            else
                [s, v] = iio_mex('channel_get_type', obj.Handle);
            end
        end

        function a = attrList(obj, h)
            %ATTRLIST 1-by-N iio.Attr of the ids H (an id 0 is left out), with this channel as parent.
            a = iio.Attr.empty(1, 0);
            h = h(h ~= 0);
            for k = 1:numel(h)
                a(k) = iio.Attr(obj, h(k));
            end
        end
    end
end
