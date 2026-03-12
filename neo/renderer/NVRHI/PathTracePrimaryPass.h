#pragma once

class idRenderBackend;   // forward declare
struct viewDef_t;

class PathTracePrimaryPass {
public:
    explicit PathTracePrimaryPass(idRenderBackend* backend);
    ~PathTracePrimaryPass();

    void Execute(const viewDef_t* viewDef);

private:
    idRenderBackend* m_backend;
};