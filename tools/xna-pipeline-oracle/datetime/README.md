# Genuine XNA DateTime oracle

`CNA-FIX-D03` measures the actual XNA 4.0 reader and compiler through reflection. It writes three
small Windows/Reach/uncompressed XNB files and records their DateTime payloads. The reader probe
also records all four two-bit Kind encodings, including the invalid value 3. No Microsoft binary
is copied into the repository.

The oracle needs `mcs`, Wine, legally obtained XNA framework/pipeline assemblies and the existing
Wine/.NET/XNA prefix used by the other interoperability harnesses. The wrapper always runs Wine
through CNA's private display runner. Exit 77 means missing prerequisites.

```bash
CNA_XNA40_REFERENCES=/path/to/xna/assemblies \
    bash tools/xna-pipeline-oracle/datetime/run-datetime-oracle.sh /tmp/xna-datetime-output
```

The default `CNA_XNA40_DATETIME_TZ=Etc/GMT-2` gives a fixed UTC+2 zone (Wine reports Kaliningrad
Standard Time). Set it explicitly to measure another zone. The committed fixtures and measured
values are in `tests/reference/xna40/datetime-kind/PROVENANCE.json`.

In the measured zone, local input ticks are reduced by two hours on write. A local payload is
converted from UTC ticks on read, while Unspecified/UTC keep their ticks and Kind. FNA's current
reader constructs Local directly from the payload ticks; this is the measured disagreement.
The probe is not native Windows qualification or a test of DST ambiguity and full asset graphs.
