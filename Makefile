CFLAGS=-DUNICODE -D_UNICODE -Wall -static-libgcc -municode
LDLIBS=-lpsapi

FILES=PatchQPF.exe pqpf014c.dll pqpf8664.dll
INSTALL=PatchQPF-install.exe

all: $(INSTALL) sysinfo.exe

clean:
	-rm $(FILES) $(INSTALL) sysinfo.exe

$(INSTALL): patchqpf.nsi $(FILES)
	strip PatchQPF.exe
	PATH=/mingw32/bin strip pqpf014c.dll
	PATH=/mingw64/bin strip pqpf8664.dll
	makensis patchqpf.nsi

PatchQPF.exe: patchqpf.c
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

sysinfo.exe: sysinfo.c
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

pqpf014c.dll: dll.c
	PATH=/mingw32/bin $(CC) $(CFLAGS) -shared $^ $(LDLIBS) -o $@ 

pqpf8664.dll: dll.c
	PATH=/mingw64/bin $(CC) $(CFLAGS) -shared $^ $(LDLIBS) -o $@ 
