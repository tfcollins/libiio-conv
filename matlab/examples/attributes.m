function t = attributes(opts)
%ATTRIBUTES Read, and optionally write, the attributes of a device.
%   T = ATTRIBUTES() opens the ADALM-Pluto at ip:192.168.2.1 and returns the
%   attributes of its device ad9361-phy as a table: one row per attribute, named
%   after it, with the value as text in the variable "value". An attribute that
%   cannot be read has the value <missing>. One value: t{'ensm_mode', 'value'}.
%
%   T = ATTRIBUTES(Set = NAME, Value = V) first writes V (text, a number or a
%   logical) to the attribute NAME. Uri and Phy select another context and device.
%   On the emulated context of the tests (matlab/tests/EmuFixture.m):
%       t = attributes(Uri = fx.Uri, Phy = "emu-phy", Set = "text", Value = "abc");
%
%   See also iio.Attr, iio.Device, list_contexts.
arguments
    opts.Uri (1,1) string = "ip:192.168.2.1"
    opts.Phy (1,1) string = "ad9361-phy"
    opts.Set (1,1) string = ""
    opts.Value = []
end
ctx = iio.Context(opts.Uri);
dev = ctx.find_device(opts.Phy);
if isempty(dev)
    error('attributes:noDevice', 'The context has no device "%s".', opts.Phy);
end
if opts.Set ~= ""
    a = dev.find_attr(opts.Set);        % 1-by-0 when the device has no such attribute
    a.write(opts.Value);                % the class of the value selects text, logical or number
end
attrs = dev.attrs;                      % 1-by-N iio.Attr, in the order of libiio
names = cell(numel(attrs), 1);
value = strings(numel(attrs), 1);
for k = 1:numel(attrs)
    names{k} = attrs(k).name;
    try
        value(k) = string(attrs(k).read());     % char; read_double, read_longlong, read_bool convert
    catch
        value(k) = missing;             % write-only, or without a value
    end
end
t = table(value, 'RowNames', names);
end
