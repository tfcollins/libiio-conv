classdef TxStream < iio.internal.Object
    %TXSTREAM Send frames of samples to a device, converted in one call.
    %   T = iio.TxStream(DEV, CHANNELS) opens the buffer of the iio.Device DEV for the
    %   output channels CHANNELS. CHANNELS is a string array, a cell array of character
    %   vectors or a character vector of channel ids, or an iio.Channel array. Its
    %   order is the column order of the data.
    %
    %   VALID = T.write(DATA) converts and queues one frame: 1 to SamplesPerFrame rows,
    %   one column per channel (per I/Q pair with Complex), of any numeric class, real
    %   or complex. Values are rounded to the nearest integer (halves to the even one)
    %   and saturate at the limits of the channel. VALID is false when every block was
    %   still in flight after the Timeout: nothing was queued, write again.
    %
    %   T = iio.TxStream(DEV, CHANNELS, Name = Value) sets options:
    %       SamplesPerFrame   largest number of rows of a frame (default 32768)
    %       Complex           true: CHANNELS lists I then Q of each column and DATA may
    %                         be complex; real DATA sends Q = 0 (default false)
    %       Scale             "none" (default): DATA holds sample values; "iio": values
    %                         in the unit of the channel; "fullscale": -1 to 1. "iio"
    %                         and "fullscale" need DATA of class double or single.
    %       Gain, Offset      a scalar, or one value per column, for DATA of class
    %                         double or single. The device gets (DATA - Offset) / Gain:
    %                         the exact inverse of iio.RxStream, which returns
    %                         sample * Gain + Offset. With Gain = 2 and Offset = 10
    %                         the value 100 is sent as (100 - 10) / 2 = 45, and 45
    %                         received with the same options is 100 again. With
    %                         Complex the Offset is taken from the real and from the
    %                         imaginary part alike.
    %       Raw               true: DATA holds the storage words as they are sent
    %       NumBlocks         frames that can be in flight at the same time (default
    %                         2, at most 4096)
    %       Cyclic            true: the first frame written repeats until the stream
    %                         is closed; a second write is an error (default false)
    %       Timeout           how long write and flush wait for a free block, in ms.
    %                         -1 (default): as long as the timeout of the context
    %                         allows; 0: not at all
    %       Buffer            position of the buffer of DEV, counted from 0. -1
    %                         (default): the first buffer that has every channel
    %
    %   The first write starts the buffer. T.flush() waits until the queued frames are
    %   sent. T.close() waits for the frames in flight and releases the buffer; so does
    %   deleting T or letting it go out of scope. That wait is the one of write: up to
    %   Timeout ms; with Timeout = -1 as long as the timeout of the context allows for
    %   each frame, and at most one second when the context has no timeout. It cannot
    %   be interrupted, and it also runs at clear all and when MATLAB exits: set no
    %   Timeout that is longer than you are ready to wait there. The stream keeps DEV
    %   and its context alive.
    %
    %   See also iio.RxStream, iio.Device, iio.Channel.

    properties (SetAccess = immutable)
        device              % the iio.Device given
        channels            % 1-by-N cell of char: the channel ids in column order
        SamplesPerFrame
        Complex
        Scale               % char
        Gain
        Offset
        Raw
        NumBlocks
        Timeout
        Cyclic
    end

    methods
        function obj = TxStream(dev, channels, opts)
            arguments
                dev (1,1) {mustBeA(dev, "iio.Device")}
                channels                      % string array / cellstr / char of channel ids, or iio.Channel array;
                                              % column order; with Complex: I then Q of each column
                opts.SamplesPerFrame (1,1) double {mustBeInteger, mustBePositive} = 32768
                opts.Complex (1,1) logical = false
                opts.Scale (1,1) string {mustBeMember(opts.Scale, ["none","iio","fullscale"])} = "none"
                opts.Gain double = double.empty(1,0)      % scalar, or one value per output column
                opts.Offset double = double.empty(1,0)
                opts.Raw (1,1) logical = false
                opts.NumBlocks (1,1) double {mustBeInteger, mustBePositive} = 2
                opts.Timeout (1,1) double {mustBeInteger, mustBeGreaterThanOrEqual(opts.Timeout, -1)} = -1   % ms; -1: the context timeout bounds the wait, 0: no wait
                opts.Buffer (1,1) double {mustBeInteger, mustBeGreaterThanOrEqual(opts.Buffer, -1)} = -1     % 0-based buffer position, -1 = automatic
                opts.Cyclic (1,1) logical = false
            end
            if isa(channels, 'iio.Channel')
                ch = reshape(iio.internal.Object.handles(channels), 1, []);
                ids = cell(1, numel(channels));
                for k = 1:numel(channels)
                    ids{k} = channels(k).id;
                end
            else
                ch = reshape(cellstr(channels), 1, []);
                ids = ch;
            end
            obj.device = dev;
            obj.channels = ids;
            obj.SamplesPerFrame = opts.SamplesPerFrame;
            obj.Complex = opts.Complex;
            obj.Scale = char(opts.Scale);
            obj.Gain = opts.Gain;
            obj.Offset = opts.Offset;
            obj.Raw = opts.Raw;
            obj.NumBlocks = opts.NumBlocks;
            obj.Timeout = opts.Timeout;
            obj.Cyclic = opts.Cyclic;
            % one struct call of values that are no cell: a scalar struct
            s = struct('output', true, 'buffer', opts.Buffer, 'samples', opts.SamplesPerFrame, ...
                'nb_blocks', opts.NumBlocks, 'cyclic', opts.Cyclic, ...
                'complex', opts.Complex, 'scale', char(opts.Scale), 'raw', opts.Raw, ...
                'gain', opts.Gain, 'offset', opts.Offset, 'timeout_ms', opts.Timeout);
            obj.Parent = dev;
            obj.Handle = iio_mex('conv_stream_open', dev.Handle, ch, s);
        end

        function valid = write(obj, data)
            %WRITE Convert and queue one frame.
            %   VALID = WRITE(T, DATA) takes 1 to SamplesPerFrame rows and one column per
            %   channel (per I/Q pair with Complex). VALID is false when no block became
            %   free within the Timeout; nothing was queued then. With Gain and Offset the
            %   device gets (DATA - Offset) / Gain (see iio.TxStream).
            valid = iio_mex('conv_stream_write', obj.Handle, data);
        end

        function tf = flush(obj)
            %FLUSH Wait until the queued frames are sent; false when the Timeout ended the wait.
            tf = iio_mex('conv_stream_flush', obj.Handle);
        end

        function close(obj)
            %CLOSE Send the frames in flight and release the buffer. Closing twice is harmless.
            if obj.Handle ~= 0
                iio_mex('conv_stream_close', obj.Handle);
            end
        end

        function delete(obj)
            %DELETE Close the stream.
            if obj.Handle ~= 0
                iio_mex('conv_stream_close', obj.Handle);
            end
        end
    end
end
