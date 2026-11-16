classdef Buffer < iio.internal.Object
    %BUFFER A buffer of an IIO device: the place its samples are read from or written to.
    %   Buffer objects are returned by the buffers property of an iio.Device; they are
    %   not created directly. A Buffer keeps its device alive.
    %
    %   output tells the direction, scan_elements lists the channels the buffer can
    %   carry, attrs and find_attr give its attributes.
    %
    %   BS = open(MASK) opens the buffer with the channels enabled in the
    %   iio.ChannelsMask MASK and returns an iio.BufferStream, from which blocks are
    %   created. ST = create_stream(MASK, SAMPLES_COUNT) returns an iio.Stream, which
    %   manages the blocks itself. For whole frames as MATLAB arrays use iio.RxStream
    %   and iio.TxStream.
    %
    %   See also iio.Device, iio.ChannelsMask, iio.BufferStream, iio.Stream.

    properties (Dependent)
        device          % iio.Device the buffer belongs to
        output          % logical: true for a transmit buffer
        attrs           % 1-by-N iio.Attr: the buffer attributes
        scan_elements   % 1-by-N iio.Channel: the channels the buffer can carry
    end

    methods
        function obj = Buffer(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
        end

        function dev = get.device(obj)
            h = iio_mex('buffer_get_device', obj.Handle);
            dev = obj.Parent;
            if ~(isa(dev, 'iio.Device') && isscalar(dev) && isvalid(dev) && dev.Handle == h)
                error('iio:EBADF', 'The device of this buffer is no longer the one it was obtained from.');
            end
        end

        function tf = get.output(obj)
            tf = iio_mex('buffer_is_output', obj.Handle);
        end

        function a = get.attrs(obj)
            a = obj.attrList(iio_mex('buffer_get_attrs', obj.Handle));
        end

        function ch = get.scan_elements(obj)
            h = iio_mex('buffer_get_scan_elements', obj.Handle);
            dev = obj.device;
            ch = iio.Channel.empty(1, 0);
            for k = 1:numel(h)
                ch(k) = iio.Channel(dev, h(k));
            end
        end

        function a = find_attr(obj, name)
            %FIND_ATTR Buffer attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Buffer")}
                name (1,1) string
            end
            a = obj.attrList(iio_mex('buffer_find_attr', obj.Handle, char(name)));
        end

        function bs = open(obj, mask)
            %OPEN Open the buffer with the channels of MASK enabled: an iio.BufferStream.
            %   The stream keeps its own copy of the mask; its mask property tells which
            %   channels the hardware really enabled.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Buffer")}
                mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
            end
            bs = iio.BufferStream(obj, mask);
        end

        function st = create_stream(obj, mask, samples_count, nb_blocks)
            %CREATE_STREAM An iio.Stream on this buffer with the channels of MASK enabled.
            %   ST = CREATE_STREAM(MASK, SAMPLES_COUNT) uses 4 blocks of SAMPLES_COUNT
            %   samples each; ST = CREATE_STREAM(MASK, SAMPLES_COUNT, NB_BLOCKS) uses
            %   NB_BLOCKS blocks (1 to 4096).
            arguments
                obj (1,1) {mustBeA(obj, "iio.Buffer")}
                mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
                samples_count (1,1) double
                nb_blocks (1,1) double = 4
            end
            st = iio.Stream(obj, mask, samples_count, nb_blocks);
        end
    end

    methods (Access = private)
        function a = attrList(obj, h)
            %ATTRLIST 1-by-N iio.Attr of the ids H (an id 0 is left out), with this buffer as parent.
            a = iio.Attr.empty(1, 0);
            h = h(h ~= 0);
            for k = 1:numel(h)
                a(k) = iio.Attr(obj, h(k));
            end
        end
    end
end
