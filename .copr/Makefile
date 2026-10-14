# Fedora COPR "make srpm" build method (Settings -> Make SRPM -> "Makefile").
# COPR calls:  make -f .copr/Makefile srpm outdir=<dir> spec=<spec>
# (it runs it from the root of the clone of the tag/commit that the webhook delivered).
#
# The tarball comes from git (git archive of the checked-out commit), so a tag v<version> gives the
# sources of exactly that tag. The Version in the spec is rewritten from CMakeLists.txt
# (project(VERSION) + TANARA_VERSION_SUFFIX) so that the SRPM cannot disagree with the code.
# A suffix such as "a" (alpha) is not a legal RPM version character sequence: it becomes "~a"
# (pre-release sort order) in the RPM Version.

outdir ?= $(CURDIR)/srpm-out
spec   ?= $(CURDIR)/packaging/rpm/tanara.spec

# package-version.sh prints project(VERSION) + TANARA_VERSION_SUFFIX; an alpha suffix gets a "~".
RAW_VERSION := $(shell bash $(CURDIR)/tools/ci/package-version.sh)
VERSION := $(shell echo '$(RAW_VERSION)' | sed 's/\([0-9]\)\([a-z]\)/\1~\2/')

.PHONY: srpm version
version:
	@echo $(VERSION)

srpm:
	@test -n "$(RAW_VERSION)" || { echo "cannot read the version from CMakeLists.txt" >&2; exit 1; }
	mkdir -p $(outdir)
	git -c safe.directory='*' archive --format=tar.gz --prefix=tanara-$(VERSION)/ -o $(outdir)/tanara-$(VERSION).tar.gz HEAD
	sed 's/^Version:.*/Version:        $(VERSION)/' $(spec) > $(outdir)/tanara.spec
	rpmbuild -bs --define "_sourcedir $(outdir)" --define "_srcrpmdir $(outdir)" \
	    --define "_builddir $(outdir)/build" --define "_rpmdir $(outdir)" --define "_specdir $(outdir)" \
	    $(outdir)/tanara.spec
