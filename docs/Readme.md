# Deskflow

Deskflow is a free and open source keyboard and mouse sharing app. Use the keyboard, mouse, or trackpad of one computer to control nearby computers, and work seamlessly between them.

This fork adds cross-platform audio forwarding. A client captures its system audio or a selected input device and sends it to the computer acting as the server, which plays it through its selected output device. By default audio follows the pointer, so only the focused client sends audio. You can enable or disable the feature and set the capture source, devices, bitrate, and focus behaviour in **Edit > Preferences > Audio**; see [Configuration](Configuration.md#audio).

Audio forwarding uses protocol messages that upstream Deskflow does not recognise, so both the server and the client must run this fork. Set `enabled` to `false` on the server when connecting an unpatched client.

[Homepage](https://deskflow.org) [Code](https://github.com/deskflow/deskflo)

## Getting help online

 - View the [wiki](https://github.com/deskflow/deskflow/wiki) Online resource

### Chat with us
 - Main discussion on Matrix: [`#deskflow:matrix.org`](https://matrix.to/#/#deskflow:matrix.org) ([Matrix clients](https://matrix.org/ecosystem/clients/))
 - Discussion also happens on IRC: `#deskflow` or `#deskflow-dev` on [Libera Chat](https://libera.chat/)
 - Start a [new discussion](https://github.com/deskflow/deskflow/discussions) on our GitHub project.

## Reporting security issues

Check [Security](Security.md) to find out how to report security issues.
