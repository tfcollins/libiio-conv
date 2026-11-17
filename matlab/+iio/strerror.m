function s = strerror(err)
%STRERROR Text of a libiio error code.
%   S = iio.strerror(ERR) returns the char row libiio prints for the error code ERR,
%   for example 'Invalid argument (-22)' for -22. The sign of ERR does not matter for
%   the text; the number is repeated as given. ERR is a whole number in the range of
%   int32 other than -2147483648.
%
%   The identifier of an error raised by this package names the same code:
%   'iio:EINVAL' is -22.
arguments
    err (1,1) double {mustBeInteger}
end
s = char(iio_mex('strerror', err));
end
