classdef ChannelsMask < iio.internal.Object
    %CHANNELSMASK The set of channels of a device that a buffer streams.
    %   MASK = iio.ChannelsMask(DEV) creates a mask for the iio.Device DEV with no
    %   channel enabled (iio:EINVAL for a device without channels). The mask has one
    %   position per channel of the device, in the order of DEV.channels.
    %
    %   Channels are enabled one by one with the enable and disable methods of
    %   iio.Channel, or all at once:
    %       mask.channels = [dev.find_channel("voltage0_i") dev.find_channel("voltage0_q")];
    %   Only scan-element channels of that device can be enabled. enabled returns the
    %   state of every position, sample_size the bytes of one sample.
    %
    %   A buffer is opened with a mask (iio.Buffer.open). The mask of an open buffer
    %   stream (iio.BufferStream.mask) tells which channels the hardware really
    %   enabled; it can be read but not changed (iio:EPERM).
    %
    %   The mask is released by destroy, when the object is deleted, or with its
    %   context. A ChannelsMask keeps its device, and with it the context, alive.
    %
    %   See also iio.Channel, iio.Device, iio.Buffer, iio.BufferStream.

    properties (Dependent)
        device          % iio.Device the mask belongs to
        channels        % get: the enabled channels (1-by-N iio.Channel, device order); set: enable exactly these
        sample_size     % double: bytes of one sample of the device with the channels of this mask
    end

    methods
        function obj = ChannelsMask(parent, handle)
            %CHANNELSMASK iio.ChannelsMask(DEV) creates a mask for the iio.Device DEV.
            %   The form ChannelsMask(PARENT, HANDLE) is internal: it wraps a mask the
            %   gateway already holds (the read-only mask of a buffer stream).
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
            if nargin == 1
                if ~(isa(parent, 'iio.Device') && isscalar(parent))
                    error('iio:EINVAL', 'iio.ChannelsMask(dev) takes one iio.Device.');
                end
                obj.Handle = iio_mex('create_channels_mask', parent.Handle);
            end
        end

        function delete(obj)
            %DELETE Release the mask (nothing for the mask of a buffer stream).
            if obj.Handle ~= 0
                iio_mex('channels_mask_destroy', obj.Handle);
            end
        end

        function destroy(obj)
            %DESTROY Release the mask now. Any later use of it raises iio:EBADF.
            %   Calling it again, or on the mask of a buffer stream, does nothing.
            arguments
                obj (1,1) iio.ChannelsMask
            end
            if obj.Handle ~= 0
                iio_mex('channels_mask_destroy', obj.Handle);
            end
        end

        function dev = get.device(obj)
            p = obj.Parent;
            if isa(p, 'iio.Device') && isscalar(p)
                dev = p;
            elseif isa(p, 'iio.BufferStream') && isscalar(p)
                dev = p.buffer.device;      % the mask of a buffer stream
            else
                error('iio:EBADF', 'This channels mask has no device.');
            end
        end

        function ch = get.channels(obj)
            tf = iio_mex('channels_mask_get', obj.Handle);
            ch = obj.device.channels;
            ch = ch(tf);
        end

        function set.channels(obj, chans)
            if isnumeric(chans) && isempty(chans)
                chans = iio.Channel.empty(1, 0);    % mask.channels = [] disables everything
            elseif ~isa(chans, 'iio.Channel')
                error('iio:EINVAL', 'The channels of a mask are given as iio.Channel objects.');
            end
            % Check every channel before the mask changes, so that a failed assignment
            % leaves it as it was: a channel of another device is iio:EINVAL here.
            for k = 1:numel(chans)
                if ~chans(k).scan_element
                    error('iio:EINVAL', 'Channel %s is not a scan element and cannot be enabled.', chans(k).id);
                end
                iio_mex('channel_is_enabled', chans(k).Handle, obj.Handle);
            end
            tf = iio_mex('channels_mask_get', obj.Handle);
            ids = iio_mex('device_get_channels', obj.device.Handle);
            for k = find(tf)
                iio_mex('channel_disable', ids(k), obj.Handle);
            end
            for k = 1:numel(chans)
                iio_mex('channel_enable', chans(k).Handle, obj.Handle);
            end
        end

        function n = get.sample_size(obj)
            n = iio_mex('device_get_sample_size', obj.device.Handle, obj.Handle);
        end

        function tf = enabled(obj)
            %ENABLED State of every channel of the device in this mask.
            %   TF = ENABLED() is a 1-by-N logical; TF(k) belongs to device.channels(k).
            arguments
                obj (1,1) iio.ChannelsMask
            end
            tf = iio_mex('channels_mask_get', obj.Handle);
        end
    end
end
