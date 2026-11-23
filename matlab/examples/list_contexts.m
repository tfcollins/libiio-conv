function t = list_contexts(opts)
%LIST_CONTEXTS Table of the IIO contexts a scan finds.
%   T = LIST_CONTEXTS() scans the local backend of libiio (the IIO and hwmon devices
%   of this machine) and returns a table with one row per context found and the variables
%   uri and description (string); 0-by-2 when nothing is found. A uri is what
%   iio.Context takes:
%       t = list_contexts();
%       ctx = iio.Context(t.uri(1));
%
%   T = LIST_CONTEXTS(Backends = LIST) scans the backends of the comma-separated
%   LIST. A backend that libiio does not have, or that cannot scan, adds no row.
%   Backends = "" is no list at all: the backends libiio scans by default, as with
%   iio.scan_contexts().
%   The libiio of deps/build-libiio.sh has no usb backend and no network scan
%   (DNS-SD), so "usb,ip" finds a device only with a libiio built with both; an
%   ADALM-Pluto is then reached by its address, iio.Context("ip:192.168.2.1").
%   The emu backend of the tests cannot scan either, so this table is 0-by-2:
%       t = list_contexts(Backends = "emu");
%
%   See also iio.scan_contexts, iio.backends, iio.Context.
arguments
    opts.Backends (1,1) string = "local"
end
% LogLevel = "error": libiio does not print a warning for a backend it skips.
r = iio.scan_contexts(opts.Backends, LogLevel = "error");   % 1-by-N struct: uri, description
uri = reshape(string({r.uri}), [], 1);
description = reshape(string({r.description}), [], 1);
t = table(uri, description);
end
