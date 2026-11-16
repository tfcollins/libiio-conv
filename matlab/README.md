# MATLAB bindings for libiio v1

Package `iio`: handle classes over one MEX file, `iio_mex`, that reach the
public functions of libiio v1's `iio.h`, plus a receive and a transmit class
(`iio.RxStream`, `iio.TxStream`) that move one frame per call straight into
and out of a MATLAB array through libiio-conv.

The bindings are tested on libiio's emulation backend (`emu:`), on its `xml:`
backend and on a scripted test backend, in MATLAB R2025b and R2023b. They have
not been run on hardware yet; see [Limitations](#limitations).

## Requirements

- Linux x86-64 (the MEX file is `iio_mex.mexa64`)
- MATLAB R2023b or later (tested: R2023b and R2025b); base MATLAB only, no
  toolbox
- GCC, CMake 3.20 or later, Ninja, Python 3 (used at build time)
- what `deps/build-libiio.sh` needs to build libiio: git, and the packages
  the CI job installs, `libxml2-dev libzstd-dev bison flex`

libiio v1 is not taken from the system. `deps/build-libiio.sh` builds the
pinned commit into `deps/install`, and the MEX file is linked against that
copy.

The pinned libiio is built with the backends `local`, `ip`, `emu` and `xml`.
It has no `usb:` and no `serial:` backend, and no DNS-SD, so a scan finds
local contexts only: reach a device by its address (`ip:192.168.2.1`).

## Build

    deps/build-libiio.sh
    cmake -S . -B build -G Ninja -DIIO_CONV_BUILD_MATLAB=ON -DMatlab_ROOT_DIR=<root>
    cmake --build build

`<root>` is the MATLAB installation, for example `/usr/local/MATLAB/R2025b`.
MATLAB is not started by the build. The result is `matlab/iio_mex.mexa64`;
`-DIIO_MEX_OUTPUT_DIR=<dir>` writes it elsewhere (every build directory
writes that one file, so two builds at the same time each need their own).
In MATLAB:

    addpath('<repo>/matlab')

The default build also registers the `test_*` commands that the test suite
uses (a scripted backend inside the MEX file). They change nothing for a
session that does not call them, but a MEX file that is handed out is built
with `-DIIO_CONV_BUILD_TESTS=OFF`.

`IIO_CONV_BUILD_MATLAB=ON` cannot be combined with `IIO_CONV_SANITIZE=ON`:
MATLAB cannot load a sanitized MEX file.

## Quick start

The names are those of an ADALM-Pluto; any device works the same way.

```matlab
ctx = iio.Context("ip:192.168.2.1");

% attribute: the receive LO frequency
phy = ctx.find_device("ad9361-phy");
lo = phy.find_channel("altvoltage0", true).find_attr("frequency");
lo.write(2400000000);
disp(lo.read())

% receive: one complex column, 16384 rows per call
rx = iio.RxStream(ctx.find_device("cf-ad9361-lpc"), ["voltage0" "voltage1"], ...
    SamplesPerFrame = 16384, OutputDataType = "int16", Complex = true);
[data, valid] = rx.read();

% transmit: one frame, repeated until the stream is closed
tone = 0.5 * exp(2j * pi * (0:16383).' / 64);
tx = iio.TxStream(ctx.find_device("cf-ad9361-dds-core-lpc"), ["voltage0" "voltage1"], ...
    SamplesPerFrame = 16384, Complex = true, Scale = "fullscale", Cyclic = true);
tx.write(tone);
```

`matlab/examples` has five functions to start from, each with defaults for
an ADALM-Pluto and a help text: `rx_capture`, `tx_cyclic`, `attributes`,
`low_level_blocks` and `list_contexts` (`addpath('<repo>/matlab/examples')`).
`list_contexts` scans the `local` backend unless told otherwise, the only
one of this build that can scan.

`help iio.RxStream`, `help iio.Attr` and so on give the reference of each
class. Members are named after the libiio C functions without their prefix
(`find_device`, `read_longlong`); name-value options are written `LikeThis`.

## Object model and lifetime

| Class | Obtained from | What it is |
|---|---|---|
| `iio.Context` | `iio.Context(uri)` | one connection; owns everything below |
| `iio.Device` | `ctx.devices`, `ctx.find_device` | a device of the context |
| `iio.Channel` | `dev.channels`, `dev.find_channel` | a channel of a device |
| `iio.Attr` | the `attrs` properties and `find_attr` methods | an attribute of a context, device, channel or buffer |
| `iio.Buffer` | `dev.buffers` | a buffer of a device, not yet open |
| `iio.RxStream`, `iio.TxStream` | their constructors | the fast path: one frame per call |
| `iio.ChannelsMask` | `iio.ChannelsMask(dev)` | the channels a buffer streams |
| `iio.BufferStream` | `buf.open(mask)` | an open buffer (low level) |
| `iio.Block` | `bs.create_block(nbytes)`, `st.next()` | a block of samples (low level) |
| `iio.Stream` | `buf.create_stream(mask, samples_count)` | libiio's own stream helper |
| `iio.EventStream` | `dev.create_event_stream()` | the events of a device |

Package functions: `iio.version`, `iio.backends`, `iio.has_backend`,
`iio.strerror`, `iio.scan_contexts`.

- Every object is a MATLAB handle object that holds a number, the id of a
  libiio object inside the MEX file. No C pointer reaches MATLAB. `a == b`
  is true when both wrap the same libiio object.
- An object keeps the object it came from alive: a stream keeps its device,
  a device its context. A context is therefore destroyed when the last
  object that came from it is gone, or at once by `delete(ctx)`.
- Destroying a context destroys everything obtained from it, streams first.
  An object that is used after that raises `iio:EBADF`; deleting it is
  silent. Nothing has to be released in a particular order.
- A context is released by `delete(ctx)` or by going out of scope. Masks,
  blocks, `iio.Stream` and event streams are released the same way and also
  have `destroy`; buffer streams and the two fast-path streams have `close`.
  Calling these twice is harmless. Devices, channels, attributes and buffers
  belong to their context and are not released one by one.
- `iio.internal.handle_count()` is the number of libiio objects the MEX file
  holds; `iio.internal.shutdown()` destroys them all, streams before
  contexts, and leaves every iio object in the workspace invalid
  (`iio:EBADF`). The same happens when MATLAB exits.
- The MEX file is locked while it holds an object, so `clear mex` does not
  unload it then, and the objects in the workspace keep working. (Each open
  buffer has a thread of libiio, which has to be gone before the code can be
  unloaded.) `clear all` deletes the workspace objects, which releases their
  libiio objects.
- After an `iio.RxStream` or `iio.TxStream` was closed, `handle_count` stays
  one above its value before the first stream was opened on that buffer: the
  entry of the buffer itself, which goes with the context.
- An argument that must be an iio object is checked, not converted: a device
  given where a mask belongs is an error of the `arguments` block (an
  identifier that starts with `MATLAB:`), raised before libiio is called.
- Masks, event streams and devices are kept in separate arrays. `[mask dev]`
  and `masks(2) = dev` are `iio.ChannelsMask(dev)` to MATLAB and create a
  second mask; `[es dev]` opens another event stream.
- Text arguments take `char` and `string` alike; text results are `char`,
  lists of text are cell arrays of `char`. A `find_*` method returns one
  object, or a 1-by-0 array of its class when nothing has that name.

## Attributes

```matlab
a = dev.find_attr("sampling_frequency");
a.read()              % char
a.read_longlong()     % int64
a.write(30720000)     % by the class of the value, see below
a.value = "manual";   % the same as a.write("manual")
```

`write(v)` chooses by the class of `v`: text goes to `write_string`, a
`logical` to `write_bool`, an integer class or a whole-number floating-point
scalar below 2^63 to `write_longlong` (so `2400000000` is sent as
`2400000000`), anything else to `write_double`. 64-bit integers never pass
through `double`: `read_longlong` returns `int64`, and `write_longlong` sends
an `int64` or `uint64` value digit for digit; a fraction or a value outside
the range of `int64` is `iio:EINVAL`.

Four things to know:

1. `read()` returns text and is not exact for a value that is not UTF-8:
   invalid bytes become U+FFFD (`char(65533)`), and the text ends at the
   first zero byte. `read_raw()` returns the bytes as libiio counted them
   (for device, channel and buffer attributes including the terminating
   zero byte).
2. `write_double(v)` (`Exact = true`, the default) sends the shortest text
   in fixed notation that reads back as exactly `v`. Every digit is sent, and
   the kernel truncates what a driver does not keep, where libiio's `"%f"`
   (`Exact = false`) rounds to six decimals: round a computed value in MATLAB
   first. A value that needs more than 40 decimals is sent with an exponent,
   which a kernel driver refuses.
3. Text with a zero character (`char(0)`) is refused with `iio:EINVAL`
   everywhere, in names as in values; it is never cut. Binary data is written
   with `write_raw`, which sends every element of a `uint8` vector.
4. The bindings do not depend on the numeric locale of the MATLAB process
   (a decimal comma) and do not change it.

`get_range()` returns `[min step max]` and `get_available()` a cell array of
`char` for an `_available` attribute. `dev.debug_attrs`, `dev.event_attrs`,
`ch.event_attrs` and the matching `find_*` methods give the other attribute
kinds; `dev.reg_read(addr)` and `dev.reg_write(addr, value)` reach registers.

## Fast path

`iio.RxStream(dev, channels, Name = Value)` opens a buffer of `dev` for input
channels and starts capturing; `iio.TxStream(dev, channels, Name = Value)`
does the same for output channels. `channels` is a string array, a cell array
of character vectors, a character vector, or an `iio.Channel` array. Its
order is the column order of the data, whatever the order in the device.

```matlab
[data, valid] = rx.read();   % SamplesPerFrame-by-C, class OutputDataType
valid = tx.write(data);      % 1 to SamplesPerFrame rows, C columns
```

| Option | Default | Meaning |
|---|---|---|
| `SamplesPerFrame` | 32768 | rows of a receive frame; the largest number of rows of a transmit frame |
| `OutputDataType` | `"double"` | receive only: `"double"`, `"single"`, `"int8"`, `"uint8"`, `"int16"`, `"uint16"`, `"int32"`, `"uint32"`, `"int64"` or `"uint64"` |
| `Complex` | `false` | `channels` lists I then Q of each column, and the data is complex. An odd number of channels is `iio:EINVAL` |
| `Scale` | `"none"` | `"none"`: the sample values. `"iio"`: in the unit of the channel (its IIO scale and offset applied). `"fullscale"`: -1 to 1. `"iio"` and `"fullscale"` need `double` or `single` |
| `Gain`, `Offset` | none | a scalar, or one value per column, for `double` and `single` only |
| `Raw` | `false` | the storage words as they are, without shift, mask and sign extension. Not with `Scale = "iio"` |
| `NumBlocks` | 2 | receive: blocks that capture in turn; transmit: frames that can be in flight. At most 4096 |
| `Timeout` | -1 | how long `read`, `write` and `flush` wait, in ms. -1: as long as the timeout of the context allows. 0: not at all. A positive value: that long |
| `Buffer` | -1 | position of the buffer of the device, counted from 0. -1: the first buffer that has every channel |
| `Cyclic` | `false` | transmit only: the first frame written repeats until the stream is closed; a second `write` is `iio:EBUSY` |

The options except `Buffer` are readable as properties of the stream
afterwards (`OutputDataType` and `Scale` as `char`), together with `device`
and `channels`.

- **`valid`.** "No data in time" is not an error. `read` then returns
  `valid = false` and `data` all zero, and can be called again. `write`
  returns `false` when every block was still in flight after the timeout:
  nothing was queued, write again.
- **Receive.** Capture starts when the stream is created; frames captured
  before the first `read` are returned first. `rx.flush()` drops them, so
  that the next `read` waits for new data.
- **Transmit.** The first `write` starts the buffer. A frame may have fewer
  rows than `SamplesPerFrame`. `data` may be of any of the ten numeric
  classes, real or complex; real data on a `Complex` stream sends Q = 0.
  Floating-point values are rounded to the nearest integer (halves to the
  even one), NaN is sent as 0, and every value saturates at the limits of
  the channel. `tx.flush()` waits until the queued frames are sent and
  returns `false` when the timeout ended the wait.
- **Gain and Offset.** On receive the result is sample × Gain + Offset. On
  transmit the device gets (DATA − Offset) / Gain, the exact inverse: with
  `Gain = 2` and `Offset = 10` the value 100 is sent as 45, and 45 is
  received as 100. With `Complex` the Offset applies to the real and to the
  imaginary part alike. `Gain = 0` is `iio:EINVAL`.
- **Timeouts.** In libiio v1 the timeout belongs to the whole context:
  `ctx.set_timeout(ms)` (or `Timeout =` of `iio.Context`) changes attribute
  I/O and every stream of that context, and `Inf` means no timeout. The
  `Timeout` option of a stream is a limit of its own, kept by polling, and
  never changes the context.
- **Closing.** `close`, `delete` and leaving scope release the buffer.
  Closing a transmit stream first waits for the frames in flight, as `write`
  does: up to `Timeout` ms; with `Timeout = -1` as long as the timeout of the
  context allows for each frame, and at most 1 s when the context timeout is
  `Inf`. That wait cannot be interrupted, and it also runs at `clear all`
  and when MATLAB exits: choose no `Timeout` longer than you are ready to
  wait there.
- **Hardware that enables more channels than asked for** is handled: sizes
  and positions follow the mask the buffer really has.
- **Error messages** of the MEX file name its own fields: `samples` is
  `SamplesPerFrame`, `nb_blocks` is `NumBlocks`, `timeout_ms` is `Timeout`,
  `class` is `OutputDataType`.
- **On the `emu:` backend** a frame must not be longer than the data file
  beside the XML file. A longer frame is only partly data although `valid`
  is true. Name `SamplesPerFrame` there (the default is meant for hardware):
  the fixture calls in the help of `rx_capture` and `low_level_blocks` in
  `matlab/examples` ask for 1000 samples, and the fixture file holds 4096.

## Low-level data path

The classes below follow libiio one to one, for what the fast path does not
cover.

```matlab
mask = iio.ChannelsMask(dev);
mask.channels = [dev.find_channel("voltage0") dev.find_channel("voltage1")];
bufs = dev.buffers;
bs = bufs(1).open(mask);
blk = bs.create_block(4096 * bs.sample_size);
blk.enqueue(); bs.start(); blk.dequeue();
data = blk.read(OutputDataType = "int16", Complex = true);
```

- `Block.read` and `Block.write` convert like the fast path and check
  `OutputDataType`, `Scale`, `Gain` and `Offset` as `iio.RxStream` and
  `iio.TxStream` do. `Channels =` selects and orders the columns; a channel
  may be listed once. `blk.bytes()` and `blk.set_bytes()` move plain bytes,
  `ch.read(blk)` and `ch.write(blk, data)` the samples of one channel.
- While a block is enqueued its memory belongs to the backend: reading or
  writing it, enqueueing it again, and a dequeue of a block that is not
  enqueued or whose buffer stream is not started, raise `iio:EPERM`.
  `dequeue(true)` returns `false` when the block is not ready.
- `iio.Block(bs, n)` creates a block of `n` bytes for `n` of any numeric
  class except `uint64`, which is taken for an internal id. `bs.create_block(n)`
  takes any numeric class.
- `bs.cancel()` leaves the stream "started" while libiio has stopped moving
  its blocks: call `stop`, then `start`, before queueing again.
- `bs.mask` is the set of channels the hardware really enabled, read-only;
  the layout of a block follows it, not the mask given to `open`.
- `enable` and `disable` of an empty row of channels (what `find_channel`
  returns for an unknown id) is `iio:EINVAL`.
- With `iio.Stream` on an output buffer, the block that `next()` returned is
  submitted by the following `next()`, so the block filled last is never
  sent: use `iio.TxStream` to transmit. After a failed `next()` no block of
  the stream can be read or written until a `next()` succeeds.
- `es.read()` of an `iio.EventStream` returns a struct, or `[]` when there is
  no event. It does not wait unless asked to (`es.read(false)`): on a real
  backend that wait has no time limit.

## Errors

Every error of libiio, of libiio-conv and of the MEX file is an `MException`
whose identifier is `iio:` followed by the errno name: `iio:EINVAL`,
`iio:ETIMEDOUT`, `iio:EBADF`, `iio:EPERM`, `iio:EBUSY`, `iio:ENOENT`,
`iio:ENOSYS`, `iio:ENOTSUP` (errno 95), `iio:EAGAIN` (errno 11); an errno
without a name is `iio:E<number>`. The message is `<command>: <text>`. Test
the identifier, not the message:

```matlab
try
    a.write(v);
catch err
    if err.identifier == "iio:EINVAL"
        % the device refused the value
    else
        rethrow(err)
    end
end
```

| Identifier | Typical cause in the bindings |
|---|---|
| `iio:EBADF` | the object was destroyed (its context is gone, or it was closed) |
| `iio:EINVAL` | an argument of the wrong class, shape or range; objects of two devices used together; an option combination that is not defined |
| `iio:EPERM` | a block or stream used in the wrong state; a change to a read-only mask or to an object that belongs to an `iio.Stream` |
| `iio:ENOENT` | a channel id in the list of `iio.RxStream` / `iio.TxStream` does not exist |

A wrong option name or option value of a MATLAB method is an error of its
`arguments` block, with an identifier that starts with `MATLAB:`.
`iio.strerror(err)` gives libiio's text for an error number.

## Class reference

One row per public function of the pinned `iio.h` (141), in the order of
`matlab/api-map.tsv`, which is checked against the header and the code by
`matlab/tools/check_api.py`. 124 functions are exposed; 12 are not (user
pointers, raw block pointers, a callback, a caller-buffer variant) and 5 are
used inside the MEX file only. The notes are those of the map; "gateway"
there means the MEX file.

| C function | MATLAB | Note |
|---|---|---|
| `iio_attr_read_raw` | `iio.Attr.read`, `iio.Attr.read_raw`, `iio.Attr.value` | text, or the bytes exactly as libiio counted them (as_bytes) |
| `iio_attr_write_raw` | `iio.Attr.write_raw` | byte count = numel of the uint8 input; no strlen |
| `iio_attr_get_name` | `iio.Attr.name` |  |
| `iio_attr_get_filename` | `iio.Attr.filename` |  |
| `iio_attr_get_static_value` | `iio.Attr.static_value` |  |
| `iio_attr_get_range` | `iio.Attr.get_range` |  |
| `iio_attr_get_available` | `iio.Attr.get_available` |  |
| `iio_attr_get_available_buf` | not exposed | caller-buffer variant of iio_attr_get_available (and it reads past the terminator of its buffer at the pinned commit); use iio.Attr.get_available |
| `iio_available_list_free` | internal | called by attr_get_available after the list is copied |
| `iio_scan` | `iio.scan_contexts` |  |
| `iio_scan_destroy` | `iio.scan_contexts` |  |
| `iio_scan_get_results_count` | `iio.scan_contexts` | list form: one command returns every element |
| `iio_scan_get_description` | `iio.scan_contexts` | list form: one command returns every element |
| `iio_scan_get_uri` | `iio.scan_contexts` | list form: one command returns every element |
| `iio_strerror` | `iio.strerror` |  |
| `iio_has_backend` | `iio.has_backend` |  |
| `iio_get_builtin_backends_count` | `iio.backends` | list form: one command returns every element |
| `iio_get_builtin_backend` | `iio.backends` | list form: one command returns every element |
| `iio_create_context` | `iio.Context` |  |
| `iio_context_destroy` | `iio.Context.delete` |  |
| `iio_context_get_version_major` | `iio.Context.version`, `iio.version` |  |
| `iio_context_get_version_minor` | `iio.Context.version`, `iio.version` |  |
| `iio_context_get_version_patch` | `iio.Context.version`, `iio.version` |  |
| `iio_context_get_version_tag` | `iio.Context.version`, `iio.version` |  |
| `iio_context_get_xml` | `iio.Context.xml` |  |
| `iio_free` | internal | called by context_get_xml after the text is copied |
| `iio_context_get_name` | `iio.Context.name` |  |
| `iio_context_get_description` | `iio.Context.description` |  |
| `iio_context_get_attrs_count` | `iio.Context.attrs` | list form: one command returns every element |
| `iio_context_get_attr` | `iio.Context.attrs` | list form: one command returns every element |
| `iio_context_find_attr` | `iio.Context.find_attr` |  |
| `iio_context_get_devices_count` | `iio.Context.devices` | list form: one command returns every element |
| `iio_context_get_device` | `iio.Context.devices` | list form: one command returns every element |
| `iio_context_find_device` | `iio.Context.find_device` |  |
| `iio_context_set_timeout` | `iio.Context.set_timeout` | the timeout belongs to the whole context |
| `iio_context_get_params` | `iio.Context.params` |  |
| `iio_context_set_data` | not exposed | C user pointer, which a MATLAB value cannot be; keep the data in your own variable, for example a dictionary keyed by ctx.uri |
| `iio_context_get_data` | not exposed | see iio_context_set_data |
| `iio_context_ping` | `iio.Context.ping` |  |
| `iio_device_get_context` | `iio.Device.context` |  |
| `iio_device_get_id` | `iio.Device.id` |  |
| `iio_device_get_name` | `iio.Device.name` |  |
| `iio_device_get_label` | `iio.Device.label` |  |
| `iio_device_get_channels_count` | `iio.Device.channels` | list form: one command returns every element |
| `iio_device_get_attrs_count` | `iio.Device.attrs` | list form: one command returns every element |
| `iio_device_get_buffers_count` | `iio.Device.buffers` | list form: one command returns every element |
| `iio_device_get_channel` | `iio.Device.channels` | list form: one command returns every element |
| `iio_device_get_attr` | `iio.Device.attrs` | list form: one command returns every element |
| `iio_device_get_buffer` | `iio.Device.buffers` | list form: one command returns every element |
| `iio_device_find_channel` | `iio.Device.find_channel` |  |
| `iio_device_find_attr` | `iio.Device.find_attr` |  |
| `iio_device_set_data` | not exposed | C user pointer; keep the data in your own variable, for example a dictionary keyed by dev.id |
| `iio_device_get_data` | not exposed | see iio_device_set_data |
| `iio_device_get_trigger` | `iio.Device.get_trigger` | no trigger assigned (-ENODEV) gives an empty result, not an error |
| `iio_device_set_trigger` | `iio.Device.set_trigger` |  |
| `iio_device_is_trigger` | `iio.Device.is_trigger` |  |
| `iio_channel_get_device` | `iio.Channel.device` |  |
| `iio_channel_get_id` | `iio.Channel.id` |  |
| `iio_channel_get_name` | `iio.Channel.name` |  |
| `iio_channel_get_label` | `iio.Channel.label` |  |
| `iio_channel_is_output` | `iio.Channel.output` |  |
| `iio_channel_is_scan_element` | `iio.Channel.scan_element` |  |
| `iio_channel_get_attrs_count` | `iio.Channel.attrs` | list form: one command returns every element |
| `iio_channel_get_attr` | `iio.Channel.attrs` | list form: one command returns every element |
| `iio_channel_find_attr` | `iio.Channel.find_attr` |  |
| `iio_channel_get_event_attrs_count` | `iio.Channel.event_attrs` | list form: one command returns every element |
| `iio_channel_get_event_attr` | `iio.Channel.event_attrs` | list form: one command returns every element |
| `iio_channel_find_event_attr` | `iio.Channel.find_event_attr` |  |
| `iio_channel_enable` | `iio.Channel.enable`, `iio.ChannelsMask.channels` |  |
| `iio_channel_disable` | `iio.Channel.disable`, `iio.ChannelsMask.channels` |  |
| `iio_channel_is_enabled` | `iio.Channel.is_enabled` |  |
| `iio_channel_read` | `iio.Channel.read` | 1-by-0 for a channel that is not enabled in the stream mask (checked by the gateway, not by libiio) |
| `iio_channel_write` | `iio.Channel.write` | 0 for a channel that is not enabled in the stream mask (checked by the gateway) |
| `iio_channel_set_data` | not exposed | C user pointer; keep per-channel data in MATLAB, for example a dictionary keyed by ch.id |
| `iio_channel_get_data` | not exposed | see iio_channel_set_data |
| `iio_channel_get_type` | `iio.Channel.type`, `iio.Channel.type_id` | name from a table generated from iio.h, and the value |
| `iio_channel_get_modifier` | `iio.Channel.modifier`, `iio.Channel.modifier_id` | name from a table generated from iio.h, and the value |
| `iio_buffer_get_device` | `iio.Buffer.device` |  |
| `iio_buffer_get_attrs_count` | `iio.Buffer.attrs` | list form: one command returns every element |
| `iio_buffer_get_attr` | `iio.Buffer.attrs` | list form: one command returns every element |
| `iio_buffer_get_scan_elements_count` | `iio.Buffer.scan_elements` | list form: one command returns every element |
| `iio_buffer_get_scan_element` | `iio.Buffer.scan_elements` | list form: one command returns every element |
| `iio_buffer_find_attr` | `iio.Buffer.find_attr` |  |
| `iio_buffer_is_output` | `iio.Buffer.output` |  |
| `iio_buffer_set_data` | not exposed | C user pointer; keep per-buffer data in MATLAB, for example in a property of your own class |
| `iio_buffer_get_data` | not exposed | see iio_buffer_set_data |
| `iio_buffer_open` | `iio.BufferStream`, `iio.Buffer.open` |  |
| `iio_buffer_close` | `iio.BufferStream.close`, `iio.BufferStream.delete` |  |
| `iio_buffer_stream_cancel` | `iio.BufferStream.cancel` |  |
| `iio_buffer_stream_start` | `iio.BufferStream.start` |  |
| `iio_buffer_stream_stop` | `iio.BufferStream.stop` |  |
| `iio_buffer_stream_get_channels_mask` | `iio.BufferStream.mask`, `iio.BufferStream.sample_size` | the mask actually in use, read-only |
| `iio_buffer_stream_create_block` | `iio.Block`, `iio.BufferStream.create_block` |  |
| `iio_block_destroy` | `iio.Block.destroy`, `iio.Block.delete` |  |
| `iio_block_get_dmabuf_fd` | `iio.Block.dmabuf_fd` |  |
| `iio_block_disable_cpu_access` | `iio.Block.disable_cpu_access` |  |
| `iio_block_start` | not exposed | raw pointer; use iio.Block.nbytes, iio.Block.bytes, iio.Block.set_bytes, iio.Block.read, iio.Block.write |
| `iio_block_first` | `iio.Block.first` | returned as a byte offset from the start of the block, not a pointer |
| `iio_block_end` | not exposed | raw pointer; use iio.Block.nbytes, iio.Block.bytes, iio.Block.set_bytes, iio.Block.read, iio.Block.write |
| `iio_block_foreach_sample` | not exposed | C callback (and it miscounts samples when a sample has tail padding at the pinned commit); use iio.Block.read(Channels=...) or iio.Channel.read(block) |
| `iio_block_enqueue` | `iio.Block.enqueue` |  |
| `iio_block_dequeue` | `iio.Block.dequeue` | not ready with nonblock gives false, not an error; iio:EPERM from libiio clears the gateway flag "enqueued" |
| `iio_block_get_buffer_stream` | `iio.Block.buffer_stream` |  |
| `iio_buffer_create_stream` | `iio.Stream`, `iio.Buffer.create_stream` |  |
| `iio_stream_cancel` | `iio.Stream.cancel` |  |
| `iio_stream_destroy` | `iio.Stream.destroy`, `iio.Stream.delete` |  |
| `iio_stream_get_next_block` | `iio.Stream.next` | the error-encoded result is checked: a failed next raises (iio:ETIMEDOUT ...) and registers nothing |
| `iio_event_get_channel` | `iio.EventStream.decode`, `iio.EventStream.read` | takes the event id; the result is re-checked because libiio returns the last channel when nothing matches |
| `iio_device_create_event_stream` | `iio.EventStream`, `iio.Device.create_event_stream` |  |
| `iio_event_stream_destroy` | `iio.EventStream.destroy`, `iio.EventStream.delete` |  |
| `iio_event_stream_read` | `iio.EventStream.read` | no event (-EBUSY, -EAGAIN, -ETIMEDOUT) gives an empty result, not an error |
| `iio_create_channels_mask` | `iio.ChannelsMask` | takes the device instead of its channel count |
| `iio_channels_mask_destroy` | `iio.ChannelsMask.destroy`, `iio.ChannelsMask.delete` |  |
| `iio_device_get_sample_size` | `iio.Device.sample_size`, `iio.ChannelsMask.sample_size`, `iio.BufferStream.sample_size` |  |
| `iio_channel_get_index` | `iio.Channel.index` |  |
| `iio_channel_get_data_format` | `iio.Channel.data_format` | returned as a struct copy |
| `iio_channel_convert` | `iio.Channel.convert` |  |
| `iio_channel_convert_inverse` | `iio.Channel.convert_inverse` |  |
| `iio_channel_refresh_format` | `iio.Channel.refresh_format` |  |
| `iio_device_get_event_attrs_count` | `iio.Device.event_attrs` | list form: one command returns every element |
| `iio_device_get_event_attr` | `iio.Device.event_attrs` | list form: one command returns every element |
| `iio_device_find_event_attr` | `iio.Device.find_event_attr` |  |
| `iio_device_get_debug_attrs_count` | `iio.Device.debug_attrs` | list form: one command returns every element |
| `iio_device_get_debug_attr` | `iio.Device.debug_attrs` | list form: one command returns every element |
| `iio_device_find_debug_attr` | `iio.Device.find_debug_attr` |  |
| `iio_device_reg_write` | `iio.Device.reg_write` |  |
| `iio_device_reg_read` | `iio.Device.reg_read` |  |
| `iio_attr_read_bool` | `iio.Attr.read_bool` |  |
| `iio_attr_read_longlong` | `iio.Attr.read_longlong` | int64 result, never through double; also reached from C by the macro iio_attr_read |
| `iio_attr_read_double` | `iio.Attr.read_double` |  |
| `iio_attr_write_string` | `iio.Attr.write_string`, `iio.Attr.write`, `iio.Attr.value` |  |
| `iio_attr_write_bool` | `iio.Attr.write_bool`, `iio.Attr.write` |  |
| `iio_attr_write_longlong` | `iio.Attr.write_longlong`, `iio.Attr.write` | integer input read exactly; also reached from C by the macro iio_attr_write |
| `iio_attr_write_double` | `iio.Attr.write_double`, `iio.Attr.write` | Exact=true (default) sends shortest exact fixed notation through iio_attr_write_string instead |
| `iio_ptr` | internal | header inline; errors reach MATLAB as `iio:<ERRNONAME>` exceptions |
| `iio_err` | internal | header inline; applied by the gateway to every pointer result |
| `iio_err_cast` | internal | header inline; nothing to cast in MATLAB |
| `hwmon_channel_get_type` | `iio.Channel.type`, `iio.Channel.type_id` | used when the device is a hwmon device |
| `iio_device_is_hwmon` | `iio.Device.is_hwmon` |  |
| `iio_event_get_type` | `iio.EventStream.decode`, `iio.EventStream.read` | takes the event id; name from a table generated from iio.h, and the value |
| `iio_event_get_direction` | `iio.EventStream.decode`, `iio.EventStream.read` | takes the event id; name from a table generated from iio.h, and the value |

## Coming from libiio v0

What the calls of the v0 API that the ADI toolboxes use become:

| libiio v0 | Here |
|---|---|
| `iio_create_context_from_uri(uri)` | `ctx = iio.Context(uri)` |
| `iio_context_destroy` | `delete(ctx)`, or let `ctx` go out of scope |
| `iio_context_set_timeout(ctx, ms)` | `ctx.set_timeout(ms)` or `iio.Context(uri, Timeout = ms)`. In v0, 0 meant "no timeout"; here that is `Inf`, and 0 is the default of the backend |
| `iio_context_find_device`, `iio_device_find_channel` | `ctx.find_device(name)`, `dev.find_channel(name, is_output)` |
| `iio_channel_attr_read`, `_read_bool`, `_read_longlong`, `_read_double`, `iio_channel_attr_write` and its typed forms | `a = ch.find_attr(name)`, then `a.read()`, `a.read_bool()`, `a.read_longlong()`, `a.read_double()`, `a.write(v)` or a typed `write_*` |
| `iio_device_attr_*` | the same on `dev.find_attr(name)` |
| `iio_device_debug_attr_*` | the same on `dev.find_debug_attr(name)` |
| `iio_channel_enable` | the channel list of `iio.RxStream` / `iio.TxStream` (low level: `ch.enable(mask)`) |
| `iio_device_set_kernel_buffers_count` | the option `NumBlocks` |
| `iio_device_create_buffer(dev, samples, cyclic)` | `iio.RxStream(dev, channels, SamplesPerFrame = samples)`, `iio.TxStream(dev, channels, SamplesPerFrame = samples, Cyclic = cyclic)` |
| `iio_buffer_refill` | `[data, valid] = rx.read()` |
| `iio_buffer_push` | `valid = tx.write(data)` |
| `iio_buffer_destroy` | `close`, or `delete` of the stream |
| `iio_buffer_get_poll_fd` | none |
| `iio_buffer_set_blocking_mode(buf, false)` | the option `Timeout = 0` |
| `iio_channel_convert` | done by `read`: `Scale = "none"` (the default) gives the converted sample values; `Raw = true` leaves it out |

## Differences from the toolboxes' data path

The base classes of the ADI toolboxes hand out the storage words of the
buffer and pair I and Q in M code. A port that must give the same numbers has
to know:

- The storage word, upper bits included, needs `Raw = true`. Without it a
  receive stream returns the sample: shifted, masked and sign-extended as the
  channel's data format says. What a device with 16-bit I/Q samples delivers
  unchanged is `OutputDataType = "int16", Complex = true, Raw = true`.
- Transmit rounds a floating-point value half to even (2.5 becomes 2), where
  MATLAB's `cast` and `int16()` round half away from zero (2.5 becomes 3).
- Input saturates at the limits of the channel; nothing wraps.
- `Scale = "iio"`, `Scale = "fullscale"`, `Gain` and `Offset` need a
  floating-point class (`double` or `single`); with an integer class they are
  `iio:EINVAL`.
- A receive frame has one column per channel (per I/Q pair with `Complex`)
  and one row per sample.

## Limitations

- No code generation yet (`codegen`, Simulink): the classes call a MEX file.
- Linux x86-64 only.
- An attribute value of 32767 bytes or more cannot be read from an iiod that
  speaks the legacy text protocol (`iio:EIO`), and a value that does not fit
  a buffer of 32 MiB cannot be read on any backend (`iio:EFBIG`).
- A blocking call cannot be interrupted with Ctrl-C. Bound it with the
  timeout of the context or the `Timeout` of the stream.
- No C pointer leaves the MEX file. Functions of other libraries that take
  an `iio_device *` or `iio_context *`, such as `ad9361_set_bb_rate` and
  `ad9361_fmcomms5_multichip_sync` of libad9361, cannot be called with these
  objects; a toolbox port has to do their work through attributes.
- `iio_context_set_data` and the other user-pointer functions, the raw block
  pointers and `iio_block_foreach_sample` have no counterpart (see the class
  reference for what to use).
- Not built: the `usb:` and `serial:` backends and network scanning (DNS-SD);
  see `deps/build-libiio.sh`.
- Not tested: the `local:` and `ip:` backends and real
  hardware; DMABUF success paths and a successful `disable_cpu_access`; a
  block queued after `cancel` on a backend that uses libiio's enqueue worker;
  `cancel` from another thread; a dequeue that timed out on libiio's generic
  block path; Ctrl-C during a blocking call; a race at process exit between
  libiio's exit handler and other MATLAB threads beyond the ten exit checks
  CI runs; a libiio v0 (`libiio.so.0`) already loaded into the MATLAB process
  by another toolbox; `parfor` workers; a block longer than the `emu:` data
  file; scan results of real backends.

## Testing

Five gates. The first four need no MATLAB licence. G1 and G4 run in every
build; G2 and G3 need a MATLAB installation and the build above with
`IIO_CONV_BUILD_MATLAB=ON` (G3 for R2023b as well when
`/usr/local/MATLAB/R2023b` exists, `-DIIO_MLINT_MIN_ROOT=<root>` names
another place). They run with

    ctest --test-dir build -L mex --output-on-failure

| Gate | What |
|---|---|
| G1 | the C sources of the MEX file, compiled against a mock of the MATLAB runtime and run with ASan and UBSan on `emu:`, `xml:` and the scripted backend |
| G2 | the real MEX file: its exports, and that it loads against MATLAB's `libmx` and `libmex` |
| G3 | MATLAB's Code Analyzer (`matlab/tools/check_mlint.sh`) on every `.m` file |
| G4 | `matlab/tools/check_api.py`: every function of `iio.h` has a row in `matlab/api-map.tsv`, every row a command and a MATLAB member that exist |
| G5 | the `matlab.unittest` suite in `matlab/tests`, run in MATLAB R2025b and R2023b by the CI job `matlab` |

G1 needs one installed locale with a decimal comma (for example
`sudo locale-gen de_DE.UTF-8`); without one its tests fail, they do not skip.
`IIO_TEST_NO_COMMA_LOCALE=1` in the environment leaves the numeric-locale
checks out instead.

The MATLAB suite needs a MEX file built with the tests enabled
(`IIO_CONV_BUILD_TESTS=ON`, the default): part of it runs on the scripted
backend. In MATLAB, from the repository root:

    addpath('matlab'); results = runtests('matlab/tests');

Each test works on its own copy of the fixture in a temporary directory and
fails if it leaves a libiio object behind.

## Troubleshooting

- **`Invalid MEX-file`**: the loader did not find a library.
  `readelf -d matlab/iio_mex.mexa64` must list `libiio.so.1` as NEEDED and a
  RUNPATH that ends in `<repo>/deps/install/lib`. The path is absolute: after
  moving the repository, or when `deps/install` was removed, build again. The
  system's libiio (v0) is never used.
- **Rebuilding while MATLAB has the MEX file loaded**: MATLAB keeps the
  loaded MEX file while iio objects exist, and `clear mex` alone does not
  unload it. Call `iio.internal.shutdown()`, then `clear mex`, then build.
- **`iio:EBADF` from an object that looks fine**: its context was destroyed
  (`delete(ctx)`, `iio.internal.shutdown()`), or the stream was closed.
- **`iio:ENOSYS` for a `test_*` command, or the test suite fails at once**:
  the MEX file was built with `-DIIO_CONV_BUILD_TESTS=OFF`.
- **A receive frame on `emu:` holds values that are not in the data file**:
  the frame is longer than the file; see [Fast path](#fast-path).
