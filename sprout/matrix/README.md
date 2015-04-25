# Project Clearwater/Matrix Gateway

This directory contains the plugin for the Project Clearwater/Matrix Gateway.

Build it as part of Sprout, following the [standard instructions](../../docs/Development.md).

Install it on a separate node from Project Clearwater, following the standard Sprout [manual install process](http://clearwater.readthedocs.org/en/latest/Manual_Install/), but then also install the matrix-gateway package with `sudo apt-get install matrix-gateway`.

Once you've done so, you can configure it via `/etc/clearwater/config`.  The important settings are

*   `sprout_hostname` - set this to a hostname that resolves to the Project Clearwater/Matrix Gateway host (not to the Project Clearwater core Sprout node)
*   `matrix_home_server` - set this to the hostname of the Matrix home server to connect to - it might currently need to be the same as `sprout_hostname` (i.e. local to the gateway)
*   `matrix_as_token` - set this to an AS token you've created on Matrix.

You'll also need to configure Project Clearwater to route to the Project Clearwater/Matrix Gateway.  Since the Project Clearwater/Matrix Gateway appears to be an IBCF from the Project Clearwater core's perspective, you must configure its BGCF (`/etc/clearwater/bgcf.json`) with routing rules, e.g.

```
{
    "routes" : [
        {   "name" : "Route requests for matrix.org to the Project Clearwater/Matrix Gateway",
            "domain" : "matrix.org",
            "route" : ["sip:matrix@<gateway hostname>:5054;transport=TCP"]
        }
    ]
}
```

You should now be able to create subscribers on Project Clearwater and then register them and make calls using a WebRTC-capable SIP client to SIP URIs of the form `sip:<userpart>@matrix.org`.  These are routed to the Project Clearwater/Matrix gateway and translated into a call to `@<userpart>:matrix.org`.
