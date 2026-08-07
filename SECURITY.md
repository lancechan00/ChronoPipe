# Security Policy

Please report vulnerabilities privately through the repository security advisory
feature. Do not open a public issue before maintainers have had time to assess it.

CPB1 CRCs detect accidental corruption; they do not authenticate data. Production
transports must provide device identity, replay protection, and confidentiality.
Never compile fixed production credentials into ChronoPipe or an example.

Readers must treat schemas, lengths, shapes, timestamps, and payloads as untrusted.
All public decoders are expected to validate integer overflow and bounds before access.
