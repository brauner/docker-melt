#!/bin/bash

# docker-melt: Minimal tool to squash all layers of a docker image into a single
#              layer 

# Authors:
# Christian Brauner <christian.brauner@mailbox.org>
#

# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.

# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

if [ $(id -u) -ne 0 ]; then
	echo "ERROR: Must run as root."
	exit 1
fi

tmpdir1=$(mktemp -d /tmp/melt_XXXX)
tmpdir2=$(mktemp -d $tmpdir1/melt_XXXX)
trap "rm -rf $tmpdir1" EXIT INT QUIT PIPE
docker pull ubuntu:latest
docker save ubuntu:latest > $tmpdir1/old.tar
docker-melt -i $tmpdir1/old.tar -o $tmpdir1/new.tar -t $tmpdir2
cat $tmpdir1/new.tar | docker import - melted
name=$(docker run --rm --hostname ubuntu melted:latest hostname)
if [ "$name" != "ubuntu" ]; then
	docker rmi melted:latest
	rm -rf $tmpdir1
	exit 1
fi

premelt=$(docker history ubuntu:latest | wc -l)
postmelt=$(docker history melted:latest | wc -l)

if [ "$postmelt" -ne 2 ] || [ "$premelt" -eq 2 ] ;then
	docker rmi melted:latest
	rm -rf $tmpdir1
	exit 1
fi

docker rmi melted:latest
rm -rf $tmpdir1
exit 0
