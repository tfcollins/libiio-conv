function tf = has_backend(name)
%HAS_BACKEND True when the libiio library has the named backend.
%   TF = iio.has_backend(NAME) returns a logical scalar. NAME is a backend name
%   without the colon of a URI, such as "ip", "usb" or "local".
%
%   See also iio.backends.
arguments
    name (1,1) string
end
tf = iio_mex('has_backend', [], char(name));
end
