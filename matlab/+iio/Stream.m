classdef Stream < iio.internal.Object
    %STREAM libiio's stream helper: a ring of blocks on one buffer, one block per call.
    %   ST = iio.Stream(BUF, MASK, SAMPLES_COUNT) opens the iio.Buffer BUF with the
    %   channels of the iio.ChannelsMask MASK and creates 4 blocks of SAMPLES_COUNT
    %   samples each. ST = iio.Stream(BUF, MASK, SAMPLES_COUNT, NB_BLOCKS) creates
    %   NB_BLOCKS blocks (1 to 4096). BUF.create_stream(MASK, SAMPLES_COUNT, NB_BLOCKS)
    %   is the same.
    %   MASK is copied: changing or destroying it afterwards has no effect on ST.
    %
    %   BLK = ST.next() returns the next iio.Block. The block belongs to the stream:
    %   it is valid until the next call to next(), which hands it back to the stream.
    %   Reading or writing it after that raises iio:EPERM; enqueue, dequeue and
    %   destroy are not available on it. After a next() that failed (for example
    %   iio:ETIMEDOUT) no block of the stream can be read or written until a next()
    %   succeeds.
    %
    %   Input buffer: the first next() queues every block and starts the buffer; each
    %   call returns a block of captured samples, in order.
    %
    %   Output buffer: the block that next() returned is submitted by the following
    %   next(), so the block filled last is never sent. Use iio.TxStream to transmit.
    %   destroy (and delete) waits until the queued blocks were sent, unless the
    %   timeout of the context is Inf: then they are cancelled, so that destroy can
    %   never wait for ever.
    %
    %   The stream is destroyed when ST is deleted or goes out of scope, and with its
    %   context. ST keeps its buffer, and through it the context, alive.
    %
    %   On the emu: backend the receive data file must exist before the first next():
    %   after a next() that failed there, destroy stalls 5 seconds per block.
    %
    %   See also iio.Buffer, iio.Block, iio.ChannelsMask, iio.RxStream, iio.TxStream.

    properties (SetAccess = immutable)
        samples_count (1,1) double = 0   % samples per block
        nb_blocks (1,1) double = 0       % blocks in the ring
    end

    properties (Dependent)
        buffer          % iio.Buffer the stream was created on
    end

    methods
        function obj = Stream(buf, mask, samples_count, nb_blocks)
            % Internal form: Stream(parent, handle) with a uint64 handle wraps that handle
            % (samples_count and nb_blocks are then 0: the gateway cannot tell them).
            arguments
                buf = []
                mask = uint64(0)
                samples_count = []
                nb_blocks (1,1) double = 4
            end
            obj@iio.internal.Object(buf, uint64(0));
            if isa(mask, 'uint64')
                if ~isscalar(mask)
                    error('iio:EINVAL', 'A handle is a uint64 scalar.');
                end
                obj.Handle = mask;
            else
                if ~(isa(buf, 'iio.Buffer') && isscalar(buf))
                    error('iio:EINVAL', 'The first argument is one iio.Buffer.');
                end
                if ~(isa(mask, 'iio.ChannelsMask') && isscalar(mask))
                    error('iio:EINVAL', 'The second argument is one iio.ChannelsMask.');
                end
                if ~(isnumeric(samples_count) && isreal(samples_count) && isscalar(samples_count))
                    error('iio:EINVAL', 'The number of samples per block is one real number.');
                end
                % Handle is set before anything else: delete releases the stream if a later line fails.
                obj.Handle = iio_mex('buffer_create_stream', buf.Handle, nb_blocks, samples_count, mask.Handle);
                obj.samples_count = double(samples_count);
                obj.nb_blocks = nb_blocks;
            end
        end

        function delete(obj)
            %DELETE Destroy the stream; its blocks become invalid.
            if obj.Handle ~= 0
                iio_mex('stream_destroy', obj.Handle);
            end
        end

        function buf = get.buffer(obj)
            buf = obj.Parent;
        end

        function blk = next(obj)
            %NEXT The next iio.Block of the stream; valid until the next call to next().
            arguments
                obj (1,1) {mustBeA(obj, "iio.Stream")}
            end
            blk = iio.Block(obj, iio_mex('stream_get_next_block', obj.Handle));
        end

        function cancel(obj)
            %CANCEL Cancel the I/O of the stream: a blocked or later next() fails.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Stream")}
            end
            iio_mex('stream_cancel', obj.Handle);
        end

        function destroy(obj)
            %DESTROY Destroy the stream now; its blocks become invalid. A second call does nothing.
            %   On an output buffer the queued blocks are sent first, unless the timeout of
            %   the context is Inf: then they are cancelled.
            arguments
                obj (1,1) {mustBeA(obj, "iio.Stream")}
            end
            if obj.Handle ~= 0
                iio_mex('stream_destroy', obj.Handle);
            end
        end
    end
end
