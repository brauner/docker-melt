This is a simple tool to merge all layers of a Docker image into a single
layer. It tries to do as little as possible to achieve the result while
preserving extended attributes, acl-permissions etc. (Given that your version
of `tar` does indeed support the corresponding flags.). Whiteout files are
deleted per default. That is a Dockerfile that contains the following
instructions:

```
RUN truncate --size 200M /somefile
RUN unlink /somefile
RUN echo bla > /somefile
```

would normally cause an image to have an extra unnecessary `200MB`. `melt` will
remove the `200MB` file `/somefile` while preserving the `/somefile` in the
last instruction. This will lead to a smaller final image.

Usage is pretty simple:

```
melt -i input.tar -o output.tar [-t tmpdir]
```

Note that in order to preserve all permissions etc. `melt` should be run as
root. The resulting image can then be imported via:

```
cat output.tar | docker import - newimage
```
