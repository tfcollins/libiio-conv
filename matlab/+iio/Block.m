classdef Block < iio.internal.Object
    %BLOCK A block of samples of a buffer stream.
    %   BLK = iio.Block(BS, NBYTES) creates a block of NBYTES bytes on the buffer stream
    %   BS (an iio.BufferStream); BS.create_block(NBYTES) does the same. NBYTES is at
    %   least BS.sample_size. A block of an iio.Stream is returned by its next method and
    %   belongs to that stream.
    %
    %   NBYTES must not be of class uint64 here: a uint64 second argument is the
    %   internal form that wraps a gateway handle, and the block it gives raises
    %   iio:EBADF at its first use. BS.create_block(NBYTES) takes any numeric class.
    %
    %   Receive:   blk.enqueue(); bs.start(); blk.dequeue(); data = blk.read();
    %   Transmit:  n = blk.write(data); blk.enqueue(n * bs.sample_size); bs.start();
    %              blk.dequeue();
    %
    %   While a block is enqueued its memory belongs to the backend: read, write, bytes,
    %   set_bytes and the read and write methods of iio.Channel raise iio:EPERM until
    %   dequeue returned true. enqueue of an enqueued block, and dequeue of a block that
    %   is not enqueued or whose buffer stream is not started, raise iio:EPERM as well.
    %
    %   read and write convert between the block and a MATLAB array with one column per
    %   channel (per I/Q pair with Complex). Integer results saturate; a floating-point
    %   value written to an integer channel is rounded half to even, not half away from
    %   zero as int16() does.
    %
    %   The block is destroyed when BLK is deleted or goes out of scope, and at the
    %   latest with its buffer stream. A Block keeps its buffer stream alive.
    %
    %   See also iio.BufferStream, iio.Stream, iio.Channel, iio.RxStream, iio.TxStream.

    properties (Dependent)
        parent          % the iio.BufferStream or iio.Stream the block came from
        nbytes          % double: size of the block in bytes
        buffer_stream   % iio.BufferStream the block belongs to
    end

    methods
        function obj = Block(parent, arg)
            arguments
                parent = []
                arg (1,1) {mustBeNumeric, mustBeReal} = uint64(0)
            end
            obj.Parent = parent;
            if isa(arg, 'uint64')
                obj.Handle = arg;       % wrap; iio.Block() and array growth: no gateway call
            elseif isa(parent, 'iio.BufferStream') && isscalar(parent)
                obj.Handle = iio_mex('buffer_stream_create_block', parent.Handle, double(arg));
            else
                error('iio:EINVAL', 'A block is created on one iio.BufferStream.');
            end
        end

        function delete(obj)
            %DELETE Destroy the block. A block of an iio.Stream is left to its stream.
            if obj.Handle ~= 0
                iio_mex('block_destroy', obj.Handle);
            end
        end

        function destroy(obj)
            %DESTROY Destroy the block now; any later use of it raises iio:EBADF.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
            end
            if obj.Handle ~= 0
                iio_mex('block_destroy', obj.Handle);
            end
        end

        function p = get.parent(obj)
            p = obj.Parent;
        end

        function n = get.nbytes(obj)
            n = iio_mex('block_size', obj.Handle);
        end

        function bs = get.buffer_stream(obj)
            h = iio_mex('block_get_buffer_stream', obj.Handle);
            p = obj.Parent;
            if isa(p, 'iio.BufferStream') && isscalar(p) && isvalid(p) && p.Handle == h
                bs = p;
            elseif h ~= 0 && isa(p, 'iio.Stream') && isscalar(p) && isvalid(p)
                bs = iio.BufferStream(p.buffer, h);
            else
                error('iio:EBADF', 'The buffer stream of this block is no longer the one it was obtained from.');
            end
        end

        function enqueue(obj, bytes_used, cyclic)
            %ENQUEUE Hand the block to the backend.
            %   ENQUEUE(BLK) queues the whole block. ENQUEUE(BLK, BYTES_USED) queues its
            %   first BYTES_USED bytes (0 = the whole block). ENQUEUE(BLK, BYTES_USED, true)
            %   queues it for cyclic transmission; a cyclic block is never dequeued.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                bytes_used (1,1) {mustBeNumeric, mustBeReal} = 0
                cyclic (1,1) logical = false
            end
            iio_mex('block_enqueue', obj.Handle, bytes_used, cyclic);
        end

        function tf = dequeue(obj, nonblock)
            %DEQUEUE Take the block back from the backend.
            %   TF = DEQUEUE(BLK) waits, for as long as the timeout of the context allows,
            %   and returns true; when no data came in time it raises iio:ETIMEDOUT and the
            %   block stays enqueued. TF = DEQUEUE(BLK, true) does not wait: false means
            %   "not ready yet".
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                nonblock (1,1) logical = false
            end
            tf = iio_mex('block_dequeue', obj.Handle, nonblock);
        end

        function data = read(obj, opts)
            %READ The samples of the block as an N-by-C array.
            %   DATA = READ(BLK) returns a double matrix with one column per enabled
            %   channel of the buffer stream, in device order, and one row per sample.
            %   Name-value options:
            %       Channels        iio.Channel array: these channels, columns in this order
            %       OutputDataType  "double" (default), "single", "int8" ... "uint64"
            %       Complex         true: channels 2k-1 and 2k are the real and imaginary
            %                       part of column k
            %       Scale           "none" (default), "iio" (the channel's scale and
            %                       offset) or "fullscale" (-1 ... 1); the last two need a
            %                       floating-point OutputDataType
            %       Raw             true: the storage word as it is, without shift and
            %                       sign extension
            %       Gain, Offset    a scalar or one value per column, for OutputDataType
            %                       "double" and "single": sample * Gain + Offset. With
            %                       Complex the Offset is added to the real and to the
            %                       imaginary part alike.
            %       Samples         at most this many rows (default: all the block holds)
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                opts.Channels (1,:) {mustBeA(opts.Channels, "iio.Channel")} = iio.Channel.empty(1, 0)
                opts.OutputDataType (1,1) string = "double"
                opts.Complex (1,1) logical = false
                opts.Scale (1,1) string = "none"
                opts.Raw (1,1) logical = false
                opts.Gain = double.empty(1, 0)
                opts.Offset = double.empty(1, 0)
                opts.Samples = double.empty(1, 0)
            end
            s = struct('channels', iio.internal.Object.handles(opts.Channels), ...
                'class', char(opts.OutputDataType), 'complex', opts.Complex, ...
                'scale', char(opts.Scale), 'raw', opts.Raw, 'gain', opts.Gain, ...
                'offset', opts.Offset, 'samples', opts.Samples);
            data = iio_mex('block_read', obj.Handle, s);
        end

        function n = write(obj, data, opts)
            %WRITE Fill the block from an N-by-C array; returns the number of rows written.
            %   N = WRITE(BLK, DATA) converts the numeric matrix DATA, one column per
            %   enabled channel (per I/Q pair when DATA is complex), into the block. N is
            %   the smaller of the rows of DATA and the samples the block holds; pass
            %   N * sample_size to enqueue. Name-value options: Channels, Scale and Raw as
            %   for READ, and
            %       Gain, Offset   a scalar or one value per column, for DATA of class
            %                 double or single. The block gets (DATA - Offset) / Gain, the
            %                 exact inverse of READ: with Gain = 2 and Offset = 10 the
            %                 value 100 is stored as (100 - 10) / 2 = 45, and READ with
            %                 the same options returns 100. With Complex the Offset is
            %                 taken from the real and from the imaginary part alike.
            %       Complex   true: a column is an I/Q pair. The default is ~isreal(DATA).
            %                 With real DATA the Q channels are written as zero: say
            %                 Complex=true when the data is complex by nature, because a
            %                 complex array whose imaginary part is all zero may arrive
            %                 as a real one. The channels must be I/Q pairs either way.
            %   A channel may be listed once only in Channels (iio:EINVAL otherwise), in
            %   READ as well.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                data
                opts.Channels (1,:) {mustBeA(opts.Channels, "iio.Channel")} = iio.Channel.empty(1, 0)
                opts.Complex (1,1) logical
                opts.Scale (1,1) string = "none"
                opts.Raw (1,1) logical = false
                opts.Gain = double.empty(1, 0)
                opts.Offset = double.empty(1, 0)
            end
            if isfield(opts, 'Complex')
                iq = opts.Complex;
            else
                iq = ~isreal(data);
            end
            s = struct('channels', iio.internal.Object.handles(opts.Channels), ...
                'complex', iq, 'scale', char(opts.Scale), 'raw', opts.Raw, ...
                'gain', opts.Gain, 'offset', opts.Offset);
            n = iio_mex('block_write', obj.Handle, data, s);
        end

        function b = bytes(obj, offset, count)
            %BYTES The bytes of the block as a uint8 column.
            %   B = BYTES(BLK) returns the whole block, B = BYTES(BLK, OFFSET) the bytes
            %   from the 0-based byte OFFSET to the end, B = BYTES(BLK, OFFSET, COUNT)
            %   COUNT bytes from OFFSET.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                offset (1,1) {mustBeNumeric, mustBeReal} = 0
                count {mustBeNumeric, mustBeReal} = []
            end
            b = iio_mex('block_get_bytes', obj.Handle, offset, count);
            b = b(:);
        end

        function n = set_bytes(obj, b, offset)
            %SET_BYTES Copy the uint8 vector B into the block at the 0-based byte OFFSET (default 0).
            %   Returns the number of bytes copied. B must fit the block.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                b
                offset (1,1) {mustBeNumeric, mustBeReal} = 0
            end
            n = iio_mex('block_set_bytes', obj.Handle, b, offset);
        end

        function off = first(obj, ch)
            %FIRST Byte offset of the first sample of channel CH in the block.
            %   Equals nbytes when CH is not enabled in the mask of the buffer stream.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                ch (1,1) {mustBeA(ch, "iio.Channel")}
            end
            off = iio_mex('block_first', obj.Handle, ch.Handle);
        end

        function fd = dmabuf_fd(obj)
            %DMABUF_FD File descriptor of the DMABUF behind the block; iio:EINVAL when it has none.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
            end
            fd = iio_mex('block_get_dmabuf_fd', obj.Handle);
        end

        function disable_cpu_access(obj, tf)
            %DISABLE_CPU_ACCESS Switch CPU access to the block off (true) or on again (false).
            %   While it is off the data of the block cannot be read or written (iio:EPERM).
            %   A backend without DMABUF blocks raises iio:ENOSYS; a block of an
            %   iio.Stream belongs to the stream and raises iio:EPERM.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Block")}
                tf (1,1) logical
            end
            iio_mex('block_disable_cpu_access', obj.Handle, tf);
        end
    end
end
