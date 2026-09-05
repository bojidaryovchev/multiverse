# Content

Deliberately empty.

Sprint 001 commits no binary assets at all. The test scene - level, geometry,
materials, input mappings - is built from C++ at `AUniverseGameMode::StartPlay`
using engine primitives, so a clean checkout builds and runs from source alone.

Two reasons:

1. While the architecture is still moving, a committed level is a snapshot that
   silently stops matching what the generator produces. Building the scene from
   the generator every run means what you see is always what the code does.
2. Binary assets in Git are expensive and effectively permanent. Git LFS is
   already configured (see `.gitattributes`) so that when real content does
   arrive it lands in LFS from its first commit rather than needing a history
   rewrite.

Real content is expected here from Sprint 002 onward.
