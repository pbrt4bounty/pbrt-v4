#include <vector>
#include <pbrt/textures.h>
#include <pbrt/materials.h>
#include <cstdlib>
#include <string>

#if defined(PBRT_WITH_OSL)
// Hardware intrinsics must load before OSL
#include <immintrin.h>
#include <OSL/oslexec.h>
#include <OSL/oslquery.h>
#include <OSL/oslclosure.h>
#include <OSL/genclosure.h>
#include <OSL/rendererservices.h>
#include <OpenImageIO/texture.h>
#include <OpenImageIO/errorhandler.h>

namespace pbrt {

// ---------------------------------------------------------
// 1. OSL Renderer Services Bridge
// ---------------------------------------------------------
class PbrtRendererServices : public OSL::RendererServices {
  public:
    bool get_matrix(OSL::ShaderGlobals* sg, OSL::Matrix44& result,
                    OSL::TransformationPtr xform, float time) override {
        return false;
    }
    bool get_inverse_matrix(OSL::ShaderGlobals* sg, OSL::Matrix44& result,
                            OSL::TransformationPtr xform, float time) override {
        return false;
    }
    bool get_matrix(OSL::ShaderGlobals* sg, OSL::Matrix44& result, OSL::ustring from,
                    float time) {
        return false;
    }
    bool get_inverse_matrix(OSL::ShaderGlobals* sg, OSL::Matrix44& result,
                            OSL::ustring to, float time) {
        return false;
    }
};

// ---------------------------------------------------------
// 2. Global OSL Engine State
// ---------------------------------------------------------
static PbrtRendererServices* oslServices = nullptr;
static OSL::ShadingSystem* shadingSystem = nullptr;
#if PBRT_IS_WINDOWS
static std::shared_ptr<OIIO::TextureSystem> textureSystem = nullptr;
#else
static OIIO::TextureSystem* textureSystem = nullptr;
#endif

enum PbrtClosureIDs {
    CLOSURE_ID_DIFFUSE     = 100,
    CLOSURE_ID_MICROFACET  = 101,
    CLOSURE_ID_DIELECTRIC  = 102,
    CLOSURE_ID_EMISSION    = 103,
    CLOSURE_ID_TRANSPARENT = 104
};

// Define the closures
struct DiffuseParams {
    OSL::Vec3 N;
    float pad;  // Acts as buffer because OSL::Vec3 uses 12 bytes while LLVM compiler operates on 16 bytes
};

struct MicrofacetParams {
    OSL::Vec3 N;
    float pad1;
    OSL::Vec3 U;
    float pad2;
    float xalpha;
    float yalpha;
    float eta;
    int refract;
};

struct DielectricParams {
    OSL::Vec3 N;
    float pad1;
    OSL::Vec3 U;
    float pad2;
    float eta;
};

// 1. Map OSL's pbrt_diffuse(N)
static OSL::ClosureParam diffuse_params[] = {
    { OSL::TypeNormal, (int)offsetof(DiffuseParams, N), nullptr, (int)sizeof(OSL::Vec3) },
    { OSL::TypeDesc(), 0, nullptr, 0 }
};

// 2. Map OSL's pbrt_microfacet(N, U, xalpha, yalpha, eta, refract)
static OSL::ClosureParam microfacet_params[] = {
    { OSL::TypeNormal, (int)offsetof(MicrofacetParams, N), nullptr, (int)sizeof(OSL::Vec3) },
    { OSL::TypeVector, (int)offsetof(MicrofacetParams, U), nullptr, (int)sizeof(OSL::Vec3) },
    { OSL::TypeFloat,  (int)offsetof(MicrofacetParams, xalpha), nullptr, (int)sizeof(float) },
    { OSL::TypeFloat,  (int)offsetof(MicrofacetParams, yalpha), nullptr, (int)sizeof(float) },
    { OSL::TypeFloat,  (int)offsetof(MicrofacetParams, eta), nullptr, (int)sizeof(float) },
    { OSL::TypeInt,    (int)offsetof(MicrofacetParams, refract), nullptr, (int)sizeof(int) },
    { OSL::TypeDesc(), 0, nullptr, 0 }
};

// 3. Map OSL's pbrt_dielectric(N, U, eta)
static OSL::ClosureParam dielectric_params[] = {
    { OSL::TypeNormal, (int)offsetof(DielectricParams, N), nullptr, (int)sizeof(OSL::Vec3) },
    { OSL::TypeVector, (int)offsetof(DielectricParams, U), nullptr, (int)sizeof(OSL::Vec3) },
    { OSL::TypeFloat,  (int)offsetof(DielectricParams, eta), nullptr, (int)sizeof(float) },
    { OSL::TypeDesc(), 0, nullptr, 0 }
};

void InitOSL() {
    if (!oslServices) {
        oslServices = new PbrtRendererServices();
        static OIIO::ErrorHandler errHandler;
        
        // Create OSL TextureSystem
        // 'true' means it will be safely shared across all rendering threads
        textureSystem = OIIO::TextureSystem::create(true);
#if PBRT_IS_WINDOWS
        shadingSystem =
            new OSL::ShadingSystem(oslServices, textureSystem.get(), &errHandler);
#else
        shadingSystem = new OSL::ShadingSystem(oslServices, textureSystem, &errHandler);
#endif
        
        // Read the OSL_SHADER_PATH environment variable to set the shader search path
        // Start with the current directory as a fallback
        std::string searchPath = ".";
        if (const char* envPath = std::getenv("OSL_SHADER_PATH")) {
        #ifdef _WIN32
            searchPath += std::string(";") + envPath;
        #else
            searchPath += std::string(":") + envPath;
        #endif
        }

        shadingSystem->attribute("searchpath:shader", searchPath);
        textureSystem->attribute("searchpath", searchPath);

        // Register the closures
        shadingSystem->register_closure("pbrt_diffuse", CLOSURE_ID_DIFFUSE, diffuse_params, nullptr, nullptr);
        shadingSystem->register_closure("pbrt_microfacet", CLOSURE_ID_MICROFACET, microfacet_params, nullptr, nullptr);
        shadingSystem->register_closure("pbrt_dielectric", CLOSURE_ID_DIELECTRIC, dielectric_params, nullptr, nullptr);
        shadingSystem->register_closure("pbrt_emission", CLOSURE_ID_EMISSION, nullptr, nullptr, nullptr);
        shadingSystem->register_closure("pbrt_transparent", CLOSURE_ID_TRANSPARENT, nullptr, nullptr, nullptr);

        printf("[OSL] Shader and Texture search path configured as: %s\n", searchPath.c_str());
    }
}

void CleanupOSL() {
    if (shadingSystem) {
        delete shadingSystem;
        shadingSystem = nullptr;
    }
    if (textureSystem) {
        OIIO::TextureSystem::destroy(textureSystem);
        textureSystem = nullptr;
    }
    if (oslServices) {
        delete oslServices;
        oslServices = nullptr;
    }
}

struct ExtractedClosure {
    int id;
    RGB weight;
    const void* data;
};

void ProcessClosureTree(const OSL::ClosureColor* closure, RGB currentWeight, std::vector<ExtractedClosure>& outClosures) {
    if (!closure) return;

    switch (closure->id) {
        case OSL::ClosureColor::MUL: {
            // A weight multiplying a closure
            auto c = (const OSL::ClosureMul*)closure;
            RGB newWeight = currentWeight * RGB(c->weight[0], c->weight[1], c->weight[2]);
            ProcessClosureTree(c->closure, newWeight, outClosures);
            break;
        }
        case OSL::ClosureColor::ADD: {
            // Two closures added together
            auto c = (const OSL::ClosureAdd*)closure;
            ProcessClosureTree(c->closureA, currentWeight, outClosures);
            ProcessClosureTree(c->closureB, currentWeight, outClosures);
            break;
        }
        default: {
            // This is an actual physical closure (like Diffuse)
            auto c = (const OSL::ClosureComponent*)closure;
            RGB finalWeight = currentWeight * RGB(c->w[0], c->w[1], c->w[2]);
            outClosures.push_back({c->id, finalWeight, c->data()});
            break;
        }
    }
}

// ---------------------------------------------------------
// 3. Texture Implementation
// ---------------------------------------------------------
struct OSLTextureState {
    OSL::ShaderGroupRef shaderGroup;
    const OSL::ShaderSymbol* coutSymbol = nullptr;  // Cache the symbol
};

// Float version of OSLTexture
OSLFloatTexture::OSLFloatTexture(const std::string& shaderName, const TextureParameterDictionary &parameters) {
    state = new OSLTextureState();
    if (!shadingSystem) return;

    state->shaderGroup = shadingSystem->ShaderGroupBegin(shaderName);

    std::string searchPath = ".";
    if (const char* envPath = std::getenv("OSL_SHADER_PATH")) {
    #ifdef _WIN32
        searchPath += std::string(";") + envPath;
    #else
        searchPath += std::string(":") + envPath;
    #endif
    }

    OSL::OSLQuery query(shaderName, searchPath);
    std::string queryErr = query.geterror();

    if (queryErr.empty()) {
        for (size_t i = 0; i < query.nparams(); ++i) {
            const OSL::OSLQuery::Parameter* param = query.getparam(i);
            
            if (!param || param->isoutput) continue; 

            std::string pName = param->name.string();

            // --- A. FLOATS ---
            if (param->type == OSL::TypeFloat) {
                float defVal = param->fdefault.empty() ? 0.0f : param->fdefault[0];
                float val = (float)parameters.GetOneFloat(pName, defVal);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeFloat, &val);
            }
            // --- B. INTEGERS ---
            else if (param->type == OSL::TypeInt) {
                int defVal = param->idefault.empty() ? 0 : param->idefault[0];
                int val = parameters.GetOneInt(pName, defVal);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeInt, &val);
            }
            // --- C. 3D TYPES (Vectors, Points, Normals, Colors) ---
            else if (param->type.basetype == OSL::TypeDesc::FLOAT && param->type.aggregate == OSL::TypeDesc::VEC3) {
                float defX = param->fdefault.size() > 0 ? param->fdefault[0] : 0.0f;
                float defY = param->fdefault.size() > 1 ? param->fdefault[1] : 0.0f;
                float defZ = param->fdefault.size() > 2 ? param->fdefault[2] : 0.0f;

                Vector3f val = parameters.GetOneVector3f(pName, Vector3f(defX, defY, defZ));
                float data[3] = { (float)val.x, (float)val.y, (float)val.z };
                shadingSystem->Parameter(*(state->shaderGroup), param->name, param->type, data);
            }
            // --- D. STRINGS ---
            else if (param->type == OSL::TypeString) {
                std::string defVal = param->sdefault.empty() ? "" : param->sdefault[0].string();
                std::string val = parameters.GetOneString(pName, defVal);
                OSL::ustring uval(val);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeString, &uval);
            }
        }
    } else {
        fprintf(stderr, "[OSL WARNING] OSLQuery could not read parameters for '%s'. Reason: %s\n", 
               shaderName.c_str(), queryErr.c_str());
    }

    bool shaderSuccess = shadingSystem->Shader(*(state->shaderGroup), "surface", shaderName.c_str(), "my_layer");
    if (!shaderSuccess) {
        fprintf(stderr, "\n[OSL ERROR] Failed to load '%s.oso'!\n\n", shaderName.c_str());
    }

    std::vector<OSL::ustring> outputs;
    outputs.push_back(OSL::ustring("my_layer.Cout"));
    shadingSystem->attribute(state->shaderGroup.get(), "renderer_outputs",
                             OSL::TypeDesc(OSL::TypeDesc::STRING, (int)outputs.size()), 
                             &outputs[0]);

    bool groupSuccess = shadingSystem->ShaderGroupEnd(*(state->shaderGroup));
    if (!groupSuccess) {
        fprintf(stderr, "\n[OSL ERROR] ShaderGroup linking failed! Check terminal.\n\n");
    }

    // Force JIT compilation before lookup
    // Without this, the final symbol table doesn't exist yet, and find_symbol will fail
    shadingSystem->optimize_all_groups();

    // Symbol lookup
    state->coutSymbol = shadingSystem->find_symbol(*(state->shaderGroup), OSL::ustring("my_layer.Cout"));
    if (!state->coutSymbol) {
        state->coutSymbol = shadingSystem->find_symbol(*(state->shaderGroup), OSL::ustring("Cout"));
    }

    if (!state->coutSymbol) {
        fprintf(stderr, "[OSL ERROR] Could not find output symbol in %s.\n", shaderName.c_str());
    } else {
        printf("[OSL SUCCESS] Successfully linked shader: %s\n", shaderName.c_str());
    }
}

Float OSLFloatTexture::Evaluate(TextureEvalContext ctx) const {
    if (!state || !state->shaderGroup || !shadingSystem || !state->coutSymbol) return 0.f;

    OSL::ShaderGlobals sg;
    memset(&sg, 0, sizeof(OSL::ShaderGlobals)); 
    
    // OSL math functions (like noise) require full Normal and Derivative data
    sg.P = OSL::Vec3(ctx.p.x, ctx.p.y, ctx.p.z); 
    sg.N = OSL::Vec3(ctx.n.x, ctx.n.y, ctx.n.z);
    sg.Ng = sg.N;
    sg.dPdx = OSL::Vec3(ctx.dpdx.x, ctx.dpdx.y, ctx.dpdx.z);
    sg.dPdy = OSL::Vec3(ctx.dpdy.x, ctx.dpdy.y, ctx.dpdy.z);
    sg.u = ctx.uv[0];
    sg.v = ctx.uv[1];

    sg.dudx = ctx.dudx;
    sg.dudy = ctx.dudy;
    sg.dvdx = ctx.dvdx;
    sg.dvdy = ctx.dvdy;

    thread_local OSL::PerThreadInfo* threadInfo = nullptr;
    if (!threadInfo) {
        threadInfo = shadingSystem->create_thread_info();
    }

    OSL::ShadingContext* shadingCtx = shadingSystem->get_context(threadInfo);
    
    // Capture whether execution actually succeeded
    bool success = shadingSystem->execute(*shadingCtx, *(state->shaderGroup), sg);
    float result = 0.0f;
    if (success) {
        result = *(const float*)shadingSystem->symbol_address(*shadingCtx, state->coutSymbol); 
    }

    shadingSystem->release_context(shadingCtx);
    return result;
}

std::string OSLFloatTexture::ToString() const {
    return "[ OSLFloatTexture ]";
}

// Spectrum version of OSLTexture
OSLSpectrumTexture::OSLSpectrumTexture(const std::string& shaderName, const TextureParameterDictionary &parameters, SpectrumType spectrumType) 
    : spectrumType(spectrumType) {
    state = new OSLTextureState();
    if (!shadingSystem) return;

    state->shaderGroup = shadingSystem->ShaderGroupBegin(shaderName);

    std::string searchPath = ".";
    if (const char* envPath = std::getenv("OSL_SHADER_PATH")) {
    #ifdef _WIN32
        searchPath += std::string(";") + envPath;
    #else
        searchPath += std::string(":") + envPath;
    #endif
    }

    OSL::OSLQuery query(shaderName, searchPath);
    std::string queryErr = query.geterror();

    if (queryErr.empty()) {
        for (size_t i = 0; i < query.nparams(); ++i) {
            const OSL::OSLQuery::Parameter* param = query.getparam(i);
            
            if (!param || param->isoutput) continue; 

            std::string pName = param->name.string();

            // --- A. FLOATS ---
            if (param->type == OSL::TypeFloat) {
                float defVal = param->fdefault.empty() ? 0.0f : param->fdefault[0];
                float val = (float)parameters.GetOneFloat(pName, defVal);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeFloat, &val);
            }
            // --- B. INTEGERS ---
            else if (param->type == OSL::TypeInt) {
                int defVal = param->idefault.empty() ? 0 : param->idefault[0];
                int val = parameters.GetOneInt(pName, defVal);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeInt, &val);
            }
            // --- C. 3D TYPES (Vectors, Points, Normals, Colors) ---
            else if (param->type.basetype == OSL::TypeDesc::FLOAT && param->type.aggregate == OSL::TypeDesc::VEC3) {
                float defX = param->fdefault.size() > 0 ? param->fdefault[0] : 0.0f;
                float defY = param->fdefault.size() > 1 ? param->fdefault[1] : 0.0f;
                float defZ = param->fdefault.size() > 2 ? param->fdefault[2] : 0.0f;

                Vector3f val = parameters.GetOneVector3f(pName, Vector3f(defX, defY, defZ));
                float data[3] = { (float)val.x, (float)val.y, (float)val.z };
                shadingSystem->Parameter(*(state->shaderGroup), param->name, param->type, data);
            }
            // --- D. STRINGS ---
            else if (param->type == OSL::TypeString) {
                std::string defVal = param->sdefault.empty() ? "" : param->sdefault[0].string();
                std::string val = parameters.GetOneString(pName, defVal);
                OSL::ustring uval(val);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeString, &uval);
            }
        }
    } else {
        fprintf(stderr, "[OSL WARNING] OSLQuery could not read parameters for '%s'. Reason: %s\n", 
               shaderName.c_str(), queryErr.c_str());
    }

    bool shaderSuccess = shadingSystem->Shader(*(state->shaderGroup), "surface", shaderName.c_str(), "my_layer");
    if (!shaderSuccess) {
        fprintf(stderr, "\n[OSL ERROR] Failed to load '%s.oso'!\n\n", shaderName.c_str());
    }

    std::vector<OSL::ustring> outputs;
    outputs.push_back(OSL::ustring("my_layer.Cout"));
    shadingSystem->attribute(state->shaderGroup.get(), "renderer_outputs",
                             OSL::TypeDesc(OSL::TypeDesc::STRING, (int)outputs.size()), 
                             &outputs[0]);

    bool groupSuccess = shadingSystem->ShaderGroupEnd(*(state->shaderGroup));
    if (!groupSuccess) {
        fprintf(stderr, "\n[OSL ERROR] ShaderGroup linking failed! Check terminal.\n\n");
    }

    // Force JIT compilation before lookup
    // Without this, the final symbol table doesn't exist yet, and find_symbol will fail
    shadingSystem->optimize_all_groups();

    // Symbol lookup
    state->coutSymbol = shadingSystem->find_symbol(*(state->shaderGroup), OSL::ustring("my_layer.Cout"));
    if (!state->coutSymbol) {
        state->coutSymbol = shadingSystem->find_symbol(*(state->shaderGroup), OSL::ustring("Cout"));
    }

    if (!state->coutSymbol) {
        fprintf(stderr, "[OSL ERROR] Could not find output symbol in %s.\n", shaderName.c_str());
    } else {
        printf("[OSL SUCCESS] Successfully linked shader: %s\n", shaderName.c_str());
    }
}

SampledSpectrum OSLSpectrumTexture::Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
    if (!state || !state->shaderGroup || !shadingSystem || !state->coutSymbol) return SampledSpectrum(0.f);

    OSL::ShaderGlobals sg;
    memset(&sg, 0, sizeof(OSL::ShaderGlobals)); 
    
    sg.P = OSL::Vec3(ctx.p.x, ctx.p.y, ctx.p.z); 
    sg.N = OSL::Vec3(ctx.n.x, ctx.n.y, ctx.n.z);
    sg.Ng = sg.N;
    sg.dPdx = OSL::Vec3(ctx.dpdx.x, ctx.dpdx.y, ctx.dpdx.z);
    sg.dPdy = OSL::Vec3(ctx.dpdy.x, ctx.dpdy.y, ctx.dpdy.z);
    sg.u = ctx.uv[0];
    sg.v = ctx.uv[1];

    sg.dudx = ctx.dudx;
    sg.dudy = ctx.dudy;
    sg.dvdx = ctx.dvdx;
    sg.dvdy = ctx.dvdy;

    thread_local OSL::PerThreadInfo* threadInfo = nullptr;
    if (!threadInfo) threadInfo = shadingSystem->create_thread_info();
    OSL::ShadingContext* shadingCtx = shadingSystem->get_context(threadInfo);
    
    bool success = shadingSystem->execute(*shadingCtx, *(state->shaderGroup), sg);
    
    RGB rgb(0, 0, 0);
    if (success) {
        const float* colorData = (const float*)shadingSystem->symbol_address(*shadingCtx, state->coutSymbol); 
        if (colorData) {
            rgb = RGB(colorData[0], colorData[1], colorData[2]);
        }
    }
    shadingSystem->release_context(shadingCtx);

    // Convert OSL RGB into pbrt Spectrum
    const RGBColorSpace *sRGB = RGBColorSpace::sRGB;
    if (spectrumType == SpectrumType::Unbounded)
        return RGBUnboundedSpectrum(*sRGB, rgb).Sample(lambda);
    else if (spectrumType == SpectrumType::Albedo)
        return RGBAlbedoSpectrum(*sRGB, Clamp(rgb, 0, 1)).Sample(lambda);
    else
        return RGBIlluminantSpectrum(*sRGB, rgb).Sample(lambda);
}

std::string OSLSpectrumTexture::ToString() const {
    return "[ OSLSpectrumTexture ]";
}

OSLMaterial::OSLMaterial(const std::string& shaderName, const TextureParameterDictionary &parameters) {
    state = new OSLTextureState();
    if (!shadingSystem) return;

    state->shaderGroup = shadingSystem->ShaderGroupBegin(shaderName);

    std::string searchPath = ".";
    if (const char* envPath = std::getenv("OSL_SHADER_PATH")) {
    #ifdef _WIN32
        searchPath += std::string(";") + envPath;
    #else
        searchPath += std::string(":") + envPath;
    #endif
    }

    OSL::OSLQuery query(shaderName, searchPath);
    std::string queryErr = query.geterror();

    if (queryErr.empty()) {
        for (size_t i = 0; i < query.nparams(); ++i) {
            const OSL::OSLQuery::Parameter* param = query.getparam(i);
            
            if (!param || param->isoutput) continue; 

            std::string pName = param->name.string();

            // --- A. FLOATS ---
            if (param->type == OSL::TypeFloat) {
                float defVal = param->fdefault.empty() ? 0.0f : param->fdefault[0];
                float val = (float)parameters.GetOneFloat(pName, defVal);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeFloat, &val);
            }
            // --- B. INTEGERS ---
            else if (param->type == OSL::TypeInt) {
                int defVal = param->idefault.empty() ? 0 : param->idefault[0];
                int val = parameters.GetOneInt(pName, defVal);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeInt, &val);
            }
            // --- C. 3D TYPES (Vectors, Points, Normals, Colors) ---
            else if (param->type.basetype == OSL::TypeDesc::FLOAT && param->type.aggregate == OSL::TypeDesc::VEC3) {
                float defX = param->fdefault.size() > 0 ? param->fdefault[0] : 0.0f;
                float defY = param->fdefault.size() > 1 ? param->fdefault[1] : 0.0f;
                float defZ = param->fdefault.size() > 2 ? param->fdefault[2] : 0.0f;

                Vector3f val = parameters.GetOneVector3f(pName, Vector3f(defX, defY, defZ));
                float data[3] = { (float)val.x, (float)val.y, (float)val.z };
                shadingSystem->Parameter(*(state->shaderGroup), param->name, param->type, data);
            }
            // --- D. STRINGS ---
            else if (param->type == OSL::TypeString) {
                std::string defVal = param->sdefault.empty() ? "" : param->sdefault[0].string();
                std::string val = parameters.GetOneString(pName, defVal);
                OSL::ustring uval(val);
                shadingSystem->Parameter(*(state->shaderGroup), param->name, OSL::TypeString, &uval);
            }
        }
    } else {
        fprintf(stderr, "[OSL WARNING] OSLQuery could not read parameters for '%s'. Reason: %s\n", 
               shaderName.c_str(), queryErr.c_str());
    }

    bool shaderSuccess = shadingSystem->Shader(*(state->shaderGroup), "surface", shaderName.c_str(), "my_layer");
    if (!shaderSuccess) {
        fprintf(stderr, "\n[OSL ERROR] Failed to load '%s.oso'!\n\n", shaderName.c_str());
    }

    // Explicitly want the closure 'Ci' instead of 'Cout'
    std::vector<OSL::ustring> outputs;
    outputs.push_back(OSL::ustring("Ci"));
    shadingSystem->attribute(state->shaderGroup.get(), "renderer_outputs",
                             OSL::TypeDesc(OSL::TypeDesc::STRING, (int)outputs.size()), 
                             &outputs[0]);

    shadingSystem->ShaderGroupEnd(*(state->shaderGroup));
    shadingSystem->optimize_all_groups();
    printf("[OSL SUCCESS] Successfully linked shader: %s\n", shaderName.c_str());
}

OSLMaterial* OSLMaterial::Create(const TextureParameterDictionary &parameters, Image *normalMap, const FileLoc *loc, Allocator alloc) {
    std::string filename = parameters.GetOneString("filename", "");
    if (filename.empty()) {
        ErrorExit(loc, "OSL material requires a \"filename\" string parameter.");
    }
    return alloc.new_object<OSLMaterial>(filename, parameters);
}

std::string OSLMaterial::ToString() const {
    return "[ OSLMaterial ]";
}

// Evaluation loop and closure extraction for OSLMaterial
template <typename TextureEvaluator>
OSLBxDF OSLMaterial::GetBxDF(TextureEvaluator texEval, MaterialEvalContext ctx, SampledWavelengths &lambda) const {
    OSLBxDF oslBxDF;

    if (!state || !state->shaderGroup || !shadingSystem) {
        return oslBxDF;
    }

    OSL::ShaderGlobals sg;
    memset(&sg, 0, sizeof(OSL::ShaderGlobals));
    
    sg.P = OSL::Vec3(ctx.p.x, ctx.p.y, ctx.p.z);
    sg.N = OSL::Vec3(ctx.ns.x, ctx.ns.y, ctx.ns.z);  // Shading normal
    sg.Ng = OSL::Vec3(ctx.n.x, ctx.n.y, ctx.n.z);    // Geometric normal
    sg.I = OSL::Vec3(-ctx.wo.x, -ctx.wo.y, -ctx.wo.z); 
    sg.dPdu = OSL::Vec3(ctx.dpdus.x, ctx.dpdus.y, ctx.dpdus.z); 
    Vector3f dpdvs = Cross(ctx.ns, ctx.dpdus);
    sg.dPdv = OSL::Vec3(dpdvs.x, dpdvs.y, dpdvs.z);
    sg.backfacing = (Dot(ctx.n, ctx.wo) < 0.f) ? 1 : 0;
    sg.u = ctx.uv[0];
    sg.v = ctx.uv[1];
    sg.dPdx = OSL::Vec3(ctx.dpdx.x, ctx.dpdx.y, ctx.dpdx.z);
    sg.dPdy = OSL::Vec3(ctx.dpdy.x, ctx.dpdy.y, ctx.dpdy.z);
    sg.dudx = ctx.dudx;
    sg.dudy = ctx.dudy;
    sg.dvdx = ctx.dvdx;
    sg.dvdy = ctx.dvdy;

    thread_local OSL::PerThreadInfo* threadInfo = nullptr;
    if (!threadInfo) threadInfo = shadingSystem->create_thread_info();
    OSL::ShadingContext* shadingCtx = shadingSystem->get_context(threadInfo);

    bool success = shadingSystem->execute(*shadingCtx, *(state->shaderGroup), sg);

    SampledSpectrum diffuseWeight(0.f);

    if (success && sg.Ci) {
        const OSL::ClosureColor* Ci = (const OSL::ClosureColor*)sg.Ci;
        std::vector<ExtractedClosure> closures;
        ProcessClosureTree(Ci, RGB(1, 1, 1), closures);

        const RGBColorSpace *sRGB = RGBColorSpace::sRGB;
        for (const auto& c : closures) {
            float r = std::isnan(c.weight.r) ? 0.f : std::max(0.f, c.weight.r);
            float g = std::isnan(c.weight.g) ? 0.f : std::max(0.f, c.weight.g);
            float b = std::isnan(c.weight.b) ? 0.f : std::max(0.f, c.weight.b);
            
            RGB rgbWeight(r, g, b);
            // Convert OSL weight to PBRT spectrum
            SampledSpectrum weight = RGBUnboundedSpectrum(*sRGB, Clamp(c.weight, 0, 1)).Sample(lambda);

            if (weight.MaxComponentValue() <= 0.0f) continue;
            
            switch (c.id) {
                case CLOSURE_ID_DIFFUSE: {
                    // Give DiffuseBxDF a white reflectance. The actual color is handled by the 'weight'.
                    oslBxDF.AddDiffuse(weight, DiffuseBxDF(SampledSpectrum(1.f)));
                    break;
                }
                case CLOSURE_ID_MICROFACET: {
                    const MicrofacetParams* p = (const MicrofacetParams*)c.data;

                    float ax = std::max(0.001f, p->xalpha);
                    float ay = std::max(0.001f, p->yalpha);
                    TrowbridgeReitzDistribution distrib(ax, ay);
                    
                    if (p->refract == 1) { 
                        // It is transmitting Glass
                        oslBxDF.AddDielectric(weight, DielectricBxDF(p->eta, distrib));
                    } else { 
                        // It is reflecting Metal. Approximate standard OSL metal using ConductorBxDF.
                        SampledSpectrum eta(p->eta); 
                        SampledSpectrum k(1.f);
                        oslBxDF.AddConductor(weight, ConductorBxDF(distrib, eta, k));
                    }
                    break;
                }
                case CLOSURE_ID_DIELECTRIC: {
                    const DielectricParams* p = (const DielectricParams*)c.data;
                    TrowbridgeReitzDistribution distrib(0.f, 0.f);  // 0 roughness = perfectly smooth
                    oslBxDF.AddDielectric(weight, DielectricBxDF(p->eta, distrib));
                    break;
                }
            }
        }
    }
    shadingSystem->release_context(shadingCtx);
    
    return oslBxDF;
}

template PBRT_CPU_GPU OSLBxDF OSLMaterial::GetBxDF(BasicTextureEvaluator, MaterialEvalContext ctx, SampledWavelengths &lambda) const;
template PBRT_CPU_GPU OSLBxDF OSLMaterial::GetBxDF(UniversalTextureEvaluator, MaterialEvalContext ctx, SampledWavelengths &lambda) const;

} // namespace pbrt
#endif //