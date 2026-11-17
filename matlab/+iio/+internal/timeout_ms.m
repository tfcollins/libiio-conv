function ms = timeout_ms(t)
%TIMEOUT_MS Timeout in milliseconds as libiio takes it.
%   MS = iio.internal.timeout_ms(T) returns -1 (no timeout) for Inf and T itself, as a
%   double, for a whole number in the range of int32. 0 is libiio's default timeout.
%   Anything else (NaN, -Inf, a fraction, a non-scalar, a non-numeric value) raises
%   iio:EINVAL.
if ~(isnumeric(t) && isscalar(t) && isreal(t))
    error('iio:EINVAL', 'A timeout is a real scalar number of milliseconds, or Inf.');
end
ms = double(t);
if ms == Inf
    ms = -1;
elseif ~(ms == fix(ms) && ms >= -2147483648 && ms <= 2147483647)
    error('iio:EINVAL', 'A timeout is a whole number of milliseconds in the range of int32, or Inf.');
end
end
