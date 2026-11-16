function r = scan_contexts(backends, opts)
%SCAN_CONTEXTS Look for IIO contexts that can be opened.
%   R = iio.scan_contexts() scans the backends libiio scans by default.
%   R = iio.scan_contexts(BACKENDS) scans the backends named in BACKENDS, a
%   comma-separated list such as "usb,ip". A backend may take an argument after "=":
%   "usb=0456:b673" limits the USB scan to that vendor and product id.
%
%   R = iio.scan_contexts(BACKENDS, Name = Value) sets parameters of the scan:
%       Timeout   milliseconds, Inf = no timeout
%       LogLevel  "nolog", "error", "warning", "info" or "debug"
%
%   R is a 1-by-N struct array with the fields uri and description (char), one
%   element per context found, and 1-by-0 when nothing is found. R(k).uri is what
%   iio.Context takes. A backend that libiio does not have, or that cannot scan, is
%   skipped without an error.
%
%   See also iio.Context, iio.backends.
arguments
    backends (1,1) string = ""
    opts.Timeout (1,1) double
    opts.LogLevel (1,1) string {mustBeMember(opts.LogLevel, ["nolog", "error", "warning", "info", "debug"])}
end
p = iio.internal.params(opts);
if nargin == 0
    h = iio_mex('scan', p, []);
else
    h = iio_mex('scan', p, char(backends));
end
release = onCleanup(@() iio_mex('scan_destroy', h));
[uris, descriptions] = iio_mex('scan_get_results', h);
r = struct('uri', uris, 'description', descriptions);
end
