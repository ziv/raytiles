# Security Policy

## Supported Versions

The library is in active development, only last version is supported.

## Reporting a Vulnerability

Please notify for **any** security issue you find in the library. You can do this
by [opening new issue](https://github.com/ziv/raytiles/issues/new) and
marking it as a security issue. We will respond to your report as soon as possible and will work with you to resolve the
issue.

## Security Best Practices

The library is allowing to bypass the TLS certificate validation, but it is not recommended to do so in production
environments. If you need to bypass the TLS certificate validation, please make sure to use it only in development
environments and never in production environments.

By default, the library will validate the TLS certificate of the server. If you need to bypass the TLS certificate
validation, you can do so by setting the `allow_insecure_tls` option to `true` when creating the
`raytiles::pool_config`.