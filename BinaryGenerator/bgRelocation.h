#pragma once
namespace bg {

enum RelocationType
{
    RelocationType_REL32,
    RelocationType_DIR32,
    RelocationType_DIR64,
};

class Relocation
{
public:
    Relocation();

    Section *section;
    uint32_t rva;
    Symbol sym;
    RelocationType type;
};

class RelocationTable
{
public:
    RelocationTable(Context *ctx);

private:
    Context *m_ctx;
};

} // namespace bg