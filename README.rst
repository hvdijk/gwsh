gwsh
====

gwsh is a POSIX shell derived from dash.

Installation
------------

Pre-built packages of gwsh are provided for Debian Bookworm and Trixie, for amd64 and arm64 architectures.

To enable the repository and install gwsh:

.. code-block:: shell

    # cat >/etc/apt/sources.list.d/gwsh.sources <<EOF
    Types: deb
    URIs: https://hvdijk.github.io/gwsh/
    Suites: trixie
    Components: main
    Signed-By:
     -----BEGIN PGP PUBLIC KEY BLOCK-----
     .
     mDMEaNs+rRYJKwYBBAHaRw8BAQdAY3OnkYqMu6a/rLlkisi5yB0Jg02cQqps4xIb
     VoPYrxO0JEhhcmFsZCB2YW4gRGlqayA8aGFyYWxkQGdpZ2F3YXR0Lm5sPoiWBBMW
     CgA+FiEECtstxWabZ4hcOQmDZfroQnXBRq8FAmjbPq0CGwMFCQWjmoAFCwkIBwIG
     FQoJCAsCBBYCAwECHgECF4AACgkQZfroQnXBRq/QGQEAkUlgXm4p9kBFoxOp88Gc
     iOO0aQ50l5nCYQXeCrIpUXQA/0j1LM9Ghxb3Hyi0sKEm1L+O12lkTmzJerYPAIQo
     TQUMuDgEaNs+rRIKKwYBBAGXVQEFAQEHQCFj0kGhJ5gS6v/5YUHII5iJajyjVcZc
     bE1woDCMp/srAwEIB4h+BBgWCgAmFiEECtstxWabZ4hcOQmDZfroQnXBRq8FAmjb
     Pq0CGwwFCQWjmoAACgkQZfroQnXBRq/ZrgEAl4qx+jb3A0PcavXDW8HM4ngV0eu6
     amGdZXHamRsJpOEA/RLwEXWjbCBc26GG5FU1t7a6o1jMuAsEf3wBcx3B9cIK
     =UtRo
     -----END PGP PUBLIC KEY BLOCK-----
    EOF
    # apt-get update
    # apt-get install gwsh