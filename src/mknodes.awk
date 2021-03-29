# Copyright (c) 1991, 1993
#     The Regents of the University of California.  All rights reserved.
# Copyright (c) 1997-2005
#     Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
# Copyright (c) 2020-2021
#     Harald van Dijk <harald@gigawatt.nl>.  All rights reserved.
#
# This code is derived from software contributed to Berkeley by
# Kenneth Almquist.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

/^[^#	]/ {
	nodetypes[nodetypes["length"]++] = $1
	nodetypes[$1] = last = $2
	if (!(last in nodedefs)) {
		nodedefs[nodedefs["length"]++] = last
		nodedefs[last] = last
	}
}

/^	/ {
	nodedefs[last, nodedefs[last, "length"]++] = $1
	nodedefs[last, $1] = $2
}

END {
	types["uchar"]      = "unsigned char "
	types["nodeptr"]    = "union node *"
	types["nodelist"]   = "struct nodelist *"
	types["int"]        = "int "
	types["string"]     = "char *"
	types["tempstring"] = "char *"

	sizes = "static const short nodesize["nodetypes["length"]"] = {\n"
	calcsize = "\tif (n == NULL)\n\t\treturn;\n"
	calcsize = calcsize "\tfuncblocksize += nodesize[n->type];\n"
	calcsize = calcsize "\tswitch (n->type) {\n"
	copy = "\tif (n == NULL)\n\t\treturn NULL;\n"
	copy = copy "\tnew = funcblock;\n"
	copy = copy "\tfuncblock = (char *) funcblock + nodesize[n->type];\n"
	copy = copy "\tswitch (n->type) {\n"

	for (i = 0; i < nodetypes["length"]; i++) {
		define = define "#define "nodetypes[i]" "i"\n"
		sizes = sizes "\tSHELL_ALIGN(sizeof (struct "nodetypes[nodetypes[i]]")),\n"
	}

	for (i = 0; i < nodedefs["length"]; i++) {
		s = nodedefs[i]
		for (j = 0; j < nodetypes["length"]; j++) {
			if (nodetypes[nodetypes[j]] == s) {
				calcsize = calcsize "\tcase "nodetypes[j]":\n"
				copy = copy "\tcase "nodetypes[j]":\n"
			}
		}
		struct = struct "struct "s"{\n"
		node = node "\tstruct "s" "s";\n"
		for (j = 0; j < nodedefs[s, "length"]; j++) {
			m = nodedefs[s, j]
			struct = struct "\t"types[nodedefs[s, m]]m";\n"
		}
		for (j = nodedefs[s, "length"]; --j >= 1; ) {
			m = nodedefs[s, j]
			if (nodedefs[s, m] == "nodeptr") {
				calcsize = calcsize "\t\tcalcsize(n->"s"."m");\n"
				copy = copy"\t\tnew->"s"."m" = copynode(n->"s"."m");\n"
			} else if (nodedefs[s, m] == "nodelist") {
				calcsize = calcsize "\t\tsizenodelist(n->"s"."m");\n"
				copy = copy"\t\tnew->"s"."m" = copynodelist(n->"s"."m");\n"
			} else if (nodedefs[s, m] == "string") {
				calcsize = calcsize "\t\tfuncstringsize += strlen(n->"s"."m") + 1;\n"
				copy = copy"\t\tnew->"s"."m" = nodesavestr(n->"s"."m");\n"
			} else if (m == "type" || nodedefs[s, m] == "tempstring") {
			} else {
				copy = copy"\t\tnew->"s"."m" = n->"s"."m";\n"
			}
		}
		struct = struct "};\n\n"
		calcsize = calcsize "\t\tbreak;\n"
		copy = copy "\t\tbreak;\n"
	}

	sizes = sizes "};\n"
	calcsize = calcsize "\t};\n"
	copy = copy "\t};\n\tnew->type = n->type;\n"

	subs["%DEFINE"]   = define
	subs["%STRUCT"]   = struct
	subs["%NODE"]     = node
	subs["%SIZES"]    = sizes
	subs["%CALCSIZE"] = calcsize
	subs["%COPY"]     = copy

	print "/*\n * This file was generated by the mknodes script.\n */\n" >nodes_c
	while (getline <nodes_c_pat > 0) {
		for (key in subs) {
			if ($1 == key) {
				$0 = subs[key]
				gsub("\n$", "")
			}
		}
		print >nodes_c
	}

	print "/*\n * This file was generated by the mknodes script.\n */\n" >nodes_h
	while (getline <nodes_h_pat > 0) {
		for (key in subs) {
			if ($1 == key) {
				$0 = subs[key]
				gsub("\n$", "")
			}
		}
		print >nodes_h
	}
}
