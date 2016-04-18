This is a simple tool to merge all layers of a Docker image into a single
layer. It tries to do as little as possible to achieve the result while
preserving extended attributes, acl-permissions etc. (Given that your version
of `tar` does indeed support the corresponding flags.). Usage is pretty simple:

```
melt -i input.tar -o output.tar [-t tmpdir]
```

The resulting image can be imported via

```
cat output.tar | docker import - newimage
```
