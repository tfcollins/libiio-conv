classdef RxStream < iio.internal.Object
    %RXSTREAM Receive frames of samples from a device, converted in one call.
    %   R = iio.RxStream(DEV, CHANNELS) opens the buffer of the iio.Device DEV for the
    %   input channels CHANNELS and starts capturing. CHANNELS is a string array, a cell
    %   array of character vectors or a character vector of channel ids, or an
    %   iio.Channel array. Its order is the column order of the data.
    %
    %   [DATA, VALID] = R.read() returns the next frame: SamplesPerFrame rows, one
    %   column per channel, of class OutputDataType. VALID is false, and DATA all zero,
    %   when no frame arrived in time; that is not an error.
    %
    %   R = iio.RxStream(DEV, CHANNELS, Name = Value) sets options:
    %       SamplesPerFrame   rows of a frame (default 32768)
    %       OutputDataType    "double" (default), "single", "int8", "uint8", "int16",
    %                         "uint16", "int32", "uint32", "int64" or "uint64"
    %       Complex           true: CHANNELS lists I then Q of each column and the data
    %                         is complex (default false)
    %       Scale             "none" (default): the sample values; "iio": in the unit
    %                         of the channel (its scale and offset applied);
    %                         "fullscale": -1 to 1. "iio" and "fullscale" need
    %                         OutputDataType "double" or "single".
    %       Gain, Offset      a scalar, or one value per column, for "double" and
    %                         "single" only: the frame holds sample * Gain + Offset.
    %                         With Complex the Offset is added to the real and to the
    %                         imaginary part alike (default: none)
    %       Raw               true: the storage words as they are, without shift,
    %                         mask and sign extension (default false)
    %       NumBlocks         blocks that capture in turn (default 2, at most 4096)
    %       Timeout           how long read waits, in ms. -1 (default): as long as the
    %                         timeout of the context allows; 0: not at all
    %       Buffer            position of the buffer of DEV, counted from 0. -1
    %                         (default): the first buffer that has every channel
    %
    %   What a device with 16-bit I/Q samples delivers unchanged is
    %   OutputDataType = "int16", Complex = true, Raw = true.
    %
    %   Capture starts when the stream is created: frames captured before the first
    %   read are returned first. R.flush() drops them. R.close() releases the buffer;
    %   so does deleting R or letting it go out of scope. The stream keeps DEV and
    %   its context alive.
    %
    %   See also iio.TxStream, iio.Device, iio.Channel.

    properties (SetAccess = immutable)
        device              % the iio.Device given
        channels            % 1-by-N cell of char: the channel ids in column order
        SamplesPerFrame
        OutputDataType      % char
        Complex
        Scale               % char
        Gain
        Offset
        Raw
        NumBlocks
        Timeout
    end

    methods
        function obj = RxStream(dev, channels, opts)
            arguments
                dev (1,1) {mustBeA(dev, "iio.Device")}
                channels                      % string array / cellstr / char of channel ids, or iio.Channel array;
                                              % column order; with Complex: I then Q of each column
                opts.SamplesPerFrame (1,1) double {mustBeInteger, mustBePositive} = 32768
                opts.OutputDataType (1,1) string {mustBeMember(opts.OutputDataType, ["double","single","int8","uint8","int16","uint16","int32","uint32","int64","uint64"])} = "double"
                opts.Complex (1,1) logical = false
                opts.Scale (1,1) string {mustBeMember(opts.Scale, ["none","iio","fullscale"])} = "none"
                opts.Gain double = double.empty(1,0)      % scalar, or one value per output column
                opts.Offset double = double.empty(1,0)
                opts.Raw (1,1) logical = false
                opts.NumBlocks (1,1) double {mustBeInteger, mustBePositive} = 2
                opts.Timeout (1,1) double {mustBeInteger, mustBeGreaterThanOrEqual(opts.Timeout, -1)} = -1   % ms; -1: the context timeout bounds the wait, 0: no wait
                opts.Buffer (1,1) double {mustBeInteger, mustBeGreaterThanOrEqual(opts.Buffer, -1)} = -1     % 0-based buffer position, -1 = automatic
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
            obj.OutputDataType = char(opts.OutputDataType);
            obj.Complex = opts.Complex;
            obj.Scale = char(opts.Scale);
            obj.Gain = opts.Gain;
            obj.Offset = opts.Offset;
            obj.Raw = opts.Raw;
            obj.NumBlocks = opts.NumBlocks;
            obj.Timeout = opts.Timeout;
            % one struct call of values that are no cell: a scalar struct
            s = struct('output', false, 'buffer', opts.Buffer, 'samples', opts.SamplesPerFrame, ...
                'nb_blocks', opts.NumBlocks, 'class', char(opts.OutputDataType), ...
                'complex', opts.Complex, 'scale', char(opts.Scale), 'raw', opts.Raw, ...
                'gain', opts.Gain, 'offset', opts.Offset, 'timeout_ms', opts.Timeout);
            obj.Parent = dev;
            obj.Handle = iio_mex('conv_stream_open', dev.Handle, ch, s);
        end

        function [data, valid] = read(obj)
            %READ The next frame.
            %   [DATA, VALID] = READ(R) returns SamplesPerFrame-by-C data of class
            %   OutputDataType, complex if Complex is set. When no frame arrived within
            %   the Timeout, VALID is false and DATA is all zero.
            [data, valid] = iio_mex('conv_stream_read', obj.Handle);
        end

        function tf = flush(obj)
            %FLUSH Drop the frames captured so far, so that the next read waits for new data.
            tf = iio_mex('conv_stream_flush', obj.Handle);
        end

        function close(obj)
            %CLOSE Stop capturing and release the buffer. Closing twice is harmless.
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
