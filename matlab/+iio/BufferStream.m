classdef BufferStream < iio.internal.Object
    %BUFFERSTREAM An open buffer: the low-level way to move blocks of samples.
    %   BS = iio.BufferStream(BUF, MASK), or BS = BUF.open(MASK), opens the iio.Buffer
    %   BUF with the channels enabled in the iio.ChannelsMask MASK. The stream keeps its
    %   own copy of the mask: MASK may be changed or destroyed afterwards.
    %
    %   BLK = create_block(NBYTES) creates an iio.Block of NBYTES bytes. start enables
    %   the buffer (at least one block must exist), stop disables it; both do nothing
    %   when the stream is already in that state. cancel makes a blocked dequeue
    %   return; the stream stays started, but libiio has stopped moving its blocks:
    %   call stop and then start before blocks are queued again.
    %
    %   mask is the set of channels the hardware really enabled, which may be more
    %   than was asked for; sample_size is the size of one sample of that set. The
    %   layout of a block follows mask, not the mask given to open.
    %
    %   close (or deleting BS) stops the stream, destroys its blocks and closes the
    %   buffer; a block of a closed stream raises iio:EBADF. A BufferStream keeps its
    %   buffer, device and context alive.
    %
    %   See also iio.Buffer, iio.Block, iio.ChannelsMask, iio.Stream, iio.RxStream, iio.TxStream.

    properties (Dependent)
        buffer        % iio.Buffer the stream was opened on
        started       % logical: start was called, and stop was not (true for the buffer stream of an iio.Stream)
        mask          % read-only iio.ChannelsMask: the channels actually enabled
        sample_size   % double: bytes per sample of that mask
    end

    methods
        function obj = BufferStream(buf, arg)
            arguments
                buf = []
                arg = uint64(0)
            end
            if isa(arg, 'iio.ChannelsMask')
                if ~(isa(buf, 'iio.Buffer') && isscalar(buf) && isscalar(arg))
                    error('iio:EINVAL', 'A buffer stream is opened on one iio.Buffer with one iio.ChannelsMask.');
                end
                obj.Parent = buf;
                obj.Handle = iio_mex('buffer_open', buf.Handle, arg.Handle);
            elseif isa(arg, 'uint64') && isscalar(arg)
                % internal form: wrap the handle (0 = none)
                obj.Parent = buf;
                obj.Handle = arg;
            else
                error('iio:EINVAL', 'The second argument is an iio.ChannelsMask.');
            end
        end

        function delete(obj)
            %DELETE Close the buffer stream; its blocks are destroyed first.
            if obj.Handle ~= 0
                iio_mex('buffer_close', obj.Handle);
            end
        end

        function close(obj)
            %CLOSE Stop the stream, destroy its blocks and close the buffer.
            %   Closing a closed stream does nothing.
            arguments
                obj (1,1) iio.BufferStream
            end
            if obj.Handle ~= 0
                iio_mex('buffer_close', obj.Handle);
            end
        end

        function buf = get.buffer(obj)
            buf = obj.Parent;
        end

        function tf = get.started(obj)
            tf = iio_mex('buffer_stream_is_started', obj.Handle);
        end

        function m = get.mask(obj)
            m = iio.ChannelsMask(obj, iio_mex('buffer_stream_get_channels_mask', obj.Handle));
        end

        function n = get.sample_size(obj)
            m = iio_mex('buffer_stream_get_channels_mask', obj.Handle);
            dev = iio_mex('buffer_get_device', obj.Parent.Handle);
            n = iio_mex('device_get_sample_size', dev, m);
        end

        function start(obj)
            %START Enable the buffer. At least one block must exist. Nothing when started.
            arguments
                obj (1,1) iio.BufferStream
            end
            iio_mex('buffer_stream_start', obj.Handle);
        end

        function stop(obj)
            %STOP Disable the buffer. Nothing when not started.
            arguments
                obj (1,1) iio.BufferStream
            end
            iio_mex('buffer_stream_stop', obj.Handle);
        end

        function cancel(obj)
            %CANCEL Make a blocked dequeue of this stream return. The stream stays started.
            %   Before blocks are queued again, call stop and then start.
            arguments
                obj (1,1) iio.BufferStream
            end
            iio_mex('buffer_stream_cancel', obj.Handle);
        end

        function blk = create_block(obj, nbytes)
            %CREATE_BLOCK An iio.Block of NBYTES bytes on this stream.
            %   NBYTES is at least sample_size; a whole number of samples is the useful choice.
            arguments
                obj (1,1) iio.BufferStream
                nbytes (1,1) double
            end
            blk = iio.Block(obj, nbytes);
        end
    end
end
