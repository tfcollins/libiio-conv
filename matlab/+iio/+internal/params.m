function p = params(opts)
%PARAMS Context parameters of a name-value struct, in the form the gateway takes.
%   P = iio.internal.params(OPTS) takes the name-value struct of an arguments block.
%   OPTS may hold any of the fields
%       Timeout           milliseconds, Inf = no timeout (see iio.internal.timeout_ms)
%       LogLevel          "nolog", "error", "warning", "info" or "debug"
%       StderrLevel       the same five names
%       TimestampLevel    the same five names
%       XmlIncludeValues  true: the context XML carries the attribute values
%   Other fields are ignored. P is [] (libiio's defaults) when OPTS has none of them,
%   else a scalar struct with timeout_ms, log_level, stderr_level, timestamp_level
%   (1 to 5) and flags, holding only the fields that were given.
p = [];
if isfield(opts, 'Timeout')
    p.timeout_ms = iio.internal.timeout_ms(opts.Timeout);
end
if isfield(opts, 'LogLevel')
    p.log_level = level(opts.LogLevel);
end
if isfield(opts, 'StderrLevel')
    p.stderr_level = level(opts.StderrLevel);
end
if isfield(opts, 'TimestampLevel')
    p.timestamp_level = level(opts.TimestampLevel);
end
if isfield(opts, 'XmlIncludeValues')
    v = opts.XmlIncludeValues;
    if ~(isscalar(v) && (islogical(v) || isnumeric(v)) && (v == 0 || v == 1))
        error('iio:EINVAL', 'XmlIncludeValues must be true or false.');
    end
    p.flags = double(v ~= 0);   % IIO_CTX_XML_INCLUDE_VALUES
end
end

function n = level(name)
%LEVEL Value of enum iio_log_level: LEVEL_NOLOG = 1 ... LEVEL_DEBUG = 5.
names = {'nolog', 'error', 'warning', 'info', 'debug'};
n = [];
if (ischar(name) && (isrow(name) || isempty(name))) || (isstring(name) && isscalar(name) && ~ismissing(name))
    n = find(strcmp(char(name), names));
end
if isempty(n)
    error('iio:EINVAL', 'A log level is one of "nolog", "error", "warning", "info", "debug".');
end
end
