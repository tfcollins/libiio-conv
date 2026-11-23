classdef StreamTimeout
    %STREAMTIMEOUT Named values of the Timeout option of iio.RxStream and iio.TxStream.
    %   The Timeout of a stream is a number of milliseconds, with two values that mean
    %   something else:
    %       iio.StreamTimeout.FromContext   -1 (the default): read, write and flush wait
    %                                       as long as the timeout of the context allows
    %       iio.StreamTimeout.NonBlocking   0: they do not wait at all
    %
    %       rx = iio.RxStream(dev, channels, Timeout = iio.StreamTimeout.NonBlocking);
    %
    %   These are not the values of iio.Context: for Timeout = of iio.Context and for
    %   ctx.set_timeout, 0 is the default of the backend and -1 or Inf is no timeout.
    %
    %   The constants are plain doubles: the Timeout property of a stream compares equal
    %   to them. The class has no objects.
    %
    %   See also iio.RxStream, iio.TxStream, iio.Context.

    properties (Constant)
        FromContext = -1    % wait as long as the timeout of the context allows
        NonBlocking = 0     % do not wait
    end

    methods (Access = private)
        function obj = StreamTimeout()
        end
    end
end
