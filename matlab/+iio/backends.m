function b = backends()
%BACKENDS Names of the backends built into the libiio library.
%   B = iio.backends() returns a 1-by-N cell array of char in libiio's order, for
%   example {'local', 'ip', 'usb', 'serial', 'xml'}. A name is the part of a context
%   URI before the colon.
%
%   See also iio.has_backend, iio.Context.
b = iio_mex('get_builtin_backends');
end
