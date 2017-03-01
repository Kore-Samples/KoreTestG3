
#include "Kore/pch.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

#include <Kore/System.h>
#include <Kore/Input/Keyboard.h>
#include <Kore/Input/Mouse.h>
#include <Kore/Input/Gamepad.h>
#include <Kore/Graphics/Graphics3.h>
#include <Kore/Audio/Mixer.h>
#include <Kore/Log.h>
#include "ObjLoader.h"

#include <dinput.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <stdio.h>

#ifdef VR_RIFT 
#include "Vr/VrInterface.h"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shlobj.h>
#include <exception>
#include <XInput.h>

#include <string>


#define DEG_2_RAD(X) ((X) * float(M_PI) / 180.0f)


namespace {


using namespace Kore;

int screenWidth  = 1280;
int screenHeight = 768;

struct MeshBuffer {
    MeshBuffer() : vertexBuffer(nullptr), indexBuffer(nullptr) {
    }
    ~MeshBuffer() {
        delete vertexBuffer;
        delete indexBuffer;
    }

	VertexBuffer* vertexBuffer;
	IndexBuffer* indexBuffer;
};

VertexStructure vertexStructure;
std::vector<MeshBuffer*> meshBuffers;
std::vector<Light*> lights;
std::vector<Texture*> textures;

// Scene parameters
std::size_t activeScene             = 0;
bool        textureMappingEnabled   = true;
bool        complexLightingEnabled  = false;
bool        rotationEnabled         = true;
bool        orthoProj               = false;
bool        fogEnabled              = false;
int         activeFogType           = LinearFog;

// Scene matrices
mat4 pMatrix, vMatrix, wMatrix;

//#define ENABLE_DEBUG_CONSOLE

#ifdef ENABLE_DEBUG_CONSOLE
static void debStep(const std::string& s) {
    std::cout << "Debug Step: " << s << std::endl;
    std::flush(std::cout);
}
#else
static void debStep(const std::string& s) {
    // dummy
}
#endif

MeshBuffer* createMeshBuffer(
    const Mesh& mesh,
    const VertexStructure& vertexStructure,
    float scale = 1.0f)
{
    MeshBuffer* meshBuffer = new MeshBuffer();

	meshBuffer->vertexBuffer = new VertexBuffer(mesh.numVertices, vertexStructure, 0);
	{
		float* vertices = meshBuffer->vertexBuffer->lock();
        {
            float*       dst = vertices;
            float const* src = mesh.vertices;

			for (int i = 0; i < mesh.numVertices; ++i) {
                // copy coord
				dst[0] = src[0] * scale;
				dst[1] = src[1] * scale;
				dst[2] = src[2] * scale;

                // copy tex-coord
				dst[3] = src[3];
				dst[4] = src[4];

                // copy normal
				dst[5] = src[5];
				dst[6] = src[6];
				dst[7] = src[7];

                dst += 8;
                src += 8;
			}
        }
		meshBuffer->vertexBuffer->unlock();
	}

	meshBuffer->indexBuffer = new IndexBuffer(mesh.numFaces * 3);
	{
		int* indices = meshBuffer->indexBuffer->lock();
		for (int i = 0; i < mesh.numFaces * 3; ++i) {
			indices[i] = mesh.indices[i];
		}
		meshBuffer->indexBuffer->unlock();
	}

    return meshBuffer;
}

void showNextScene() {
    ++activeScene;
    if (activeScene >= meshBuffers.size()) {
        activeScene = 0;
    }
}

void showPrevScene() {
    if (activeScene == 0) {
        activeScene = meshBuffers.size() - 1;
    } else {
        --activeScene;
    }
}

Light* addPointLight(const vec3& position, const vec3& color, float radius = 100.0f) {
    Light* lit = new Light(PointLight);

    const vec4 ambient(1, 1, 1, 1);
    const vec4 diffuse(color[0], color[1], color[2], 1);
    const vec4 specular(1, 1, 1, 1);

    lit->setPosition(position);
    lit->setAttenuationRadius(radius);
    lit->setColors(ambient, diffuse, specular);

    lights.push_back(lit);

    return lit;
}

Light* addSpotLight(const vec3& position, const vec3& color, float spotExponent, float spotCutoff, float radius = 100.0f) {
    Light* lit = new Light(SpotLight);

    const vec4 ambient(1, 1, 1, 1);
    const vec4 diffuse(color[0], color[1], color[2], 1);
    const vec4 specular(1, 1, 1, 1);

    lit->setPosition(position);
    lit->setAttenuationRadius(radius);
    lit->setSpot(spotExponent, spotCutoff);
    lit->setColors(ambient, diffuse, specular);

    lights.push_back(lit);

    return lit;
}

Texture* addTexture(const std::string& filename) {
    Texture* tex = new Texture(filename.c_str());
    tex->generateMipmaps(0);
    textures.push_back(tex);
    return tex;
}

MeshBuffer* addMesh(const std::string& filename, float scale = 1.0f) {
    debStep("Load Mesh \"" + filename + "\"");
    Mesh* mesh = loadObj(filename.c_str());
    meshBuffers.push_back(createMeshBuffer(*mesh, vertexStructure, scale));
    delete mesh;
    return meshBuffers.back();
}

mat4 rightHandedPerspectiveProjection(float fov, float aspect, float nearPlane, float farPlane) {
    mat4 m = mat4::Identity();

    float h = 1.0f / std::tan(fov * 0.5f);
    float w = h / aspect;

    m.Set(0, 0, w);
    m.Set(1, 1, h);

    m.Set(2, 2, -(farPlane + nearPlane)/(farPlane - nearPlane));
    m.Set(2, 3, -(2.0f*farPlane*nearPlane)/(farPlane - nearPlane));

    m.Set(3, 2, -1.0f);
    m.Set(3, 3, 0.0f);

    return m;
}

void updateProjection() {
    // Initialize matrices
    float aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    if (orthoProj) {
        pMatrix = mat4::orthogonalProjection(-aspectRatio, aspectRatio, -1.0f, 1.0f, -4.0f, 4.0f);
    } else {
        //pMatrix = rightHandedPerspectiveProjection(DEG_2_RAD(45.0f), aspectRatio, 0.1f, 100.0f);
        pMatrix = mat4::Perspective(DEG_2_RAD(45.0f), aspectRatio, 0.1f, 100.0f);
    }

    vMatrix = mat4::Translation(0, 0.0f, -2.5f);

    wMatrix = mat4::Identity();
}

void initScene() {
    #ifdef ENABLE_DEBUG_CONSOLE
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    #endif

    debStep("Start");

    updateProjection();

    // Initializer render states
	Graphics3::setRenderState(DepthTest, true);
    Graphics3::setRenderState(DepthWrite, true);
	Graphics3::setRenderState(DepthTestCompare, ZCompareLess);
    Graphics3::setRenderState(Lighting, true);
    Graphics3::setRenderState(Normalize, true);

    // Initialize material states
    Graphics3::setMaterialState(SpecularColor, vec4(1, 1, 1, 1));
    Graphics3::setMaterialState(ShininessExponent, 180.0f);

    debStep("Init Render States Done");

    // Load mesh and create vertex- and index buffers
	vertexStructure.add(VertexCoord, Float3VertexData);
	vertexStructure.add(VertexTexCoord0, Float2VertexData);
	vertexStructure.add(VertexNormal, Float3VertexData);

    addMesh("Text_FixedFunctionOpenGL.obj", 0.4f);
    addMesh("UnderTessellatedCube.obj", 0.4f);
    addMesh("TessellatedCube.obj", 0.4f);
    addMesh("TessellatedCube_Bumped2.obj", 0.4f);
    addMesh("Terrain.obj", 1.0f);
    addMesh("TessellatedPlane.obj", 1.0f);

    // Create light source
    addPointLight(vec3(0, 0, 1.7), vec3(1, 1, 1));

    const float spotLightDist = 0.35f;
    addSpotLight(vec3(0, spotLightDist, 1), vec3(1, 0.2f, 0.2f), 128.0f, 15.0f);//->setDirection(vec3(0, -0.2f, 1).normalize());
    addSpotLight(vec3(-spotLightDist, -spotLightDist, 1), vec3(0.2f, 1, 0.2f), 90, 25.0f);
    addSpotLight(vec3(spotLightDist, -spotLightDist, 1), vec3(0.2f, 0.02f, 1), 35.0f, 35.0f);

    // Load textures
    addTexture("SeriousGamesTexture.png");
    addTexture("SphereMap1.jpg");
    addTexture("Grass.jpg");
    addTexture("Metal.jpg");

    debStep("Loading Textures Done");


}

void releaseScene() {
    for (std::vector<MeshBuffer*>::iterator it = meshBuffers.begin(); it != meshBuffers.end(); ++it)
        delete (*it);
    meshBuffers.clear();

    for (std::vector<Light*>::iterator it = lights.begin(); it != lights.end(); ++it)
        delete (*it);
    lights.clear();

    for (std::vector<Texture*>::iterator it = textures.begin(); it != textures.end(); ++it)
        delete (*it);
    textures.clear();
}

void onDrawFrame() {
	Audio::update();
		
	Graphics3::begin();
	Graphics3::clear(Graphics3::ClearColorFlag | Graphics3::ClearDepthFlag, 0xff808080);
		
    static float angle;
    if(rotationEnabled) angle += 0.5f;
	wMatrix = mat4::RotationY(DEG_2_RAD(std::sin(DEG_2_RAD(angle*1.5f))*75.0f));
    //wMatrix = mat4::RotationY(DEG_2_RAD(angle));

    // Initailize face culling
    //Graphics3::setRenderState(BackfaceCulling, Clockwise); // for right-handed coordinate systems
    Graphics3::setRenderState(BackfaceCulling, CounterClockwise);

    // Setup projection
    Graphics3::setProjectionMatrix(pMatrix);

    // Setup lights
    Graphics3::setViewMatrix(mat4::Identity());
    Graphics3::setWorldMatrix(mat4::Identity());

    int lightID = 0;
    for (std::size_t i = 0, n = lights.size(); (lightID < 8 && i < n); ++i) {
        Light* lit = lights[i];
        if ( ( complexLightingEnabled && i > 0 ) ||
                ( !complexLightingEnabled && i == 0 ) )
        {
            Graphics3::setLight(lights[i], lightID++);
        }
    }
		
    for (; lightID < 8; ++lightID) {
        Graphics3::setLight(nullptr, lightID);
    }
		
    // Setup texture mapping
    TextureUnit texUnit0;
    texUnit0.unit = 0;

    if (textureMappingEnabled)
    {
        Graphics3::setTextureMipmapFilter(texUnit0, LinearMipFilter);

        if (activeScene >= 1 && activeScene <= 2)
        {
            Graphics3::setTexture(texUnit0, textures[0]);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordX, TexGenDisabled);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordY, TexGenDisabled);
            Graphics3::setTextureMapping(texUnit0, Texture2D, true);
        }
        else if (activeScene == 3)
        {
            Graphics3::setTexture(texUnit0, textures[1]);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordX, TexGenSphereMap);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordY, TexGenSphereMap);
            Graphics3::setTextureMapping(texUnit0, Texture2D, true);
        }
        else if (activeScene == 4)
        {
            Graphics3::setTexture(texUnit0, textures[2]);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordX, TexGenDisabled);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordY, TexGenDisabled);
            Graphics3::setTextureMapping(texUnit0, Texture2D, true);
        }
        else if (activeScene == 5)
        {
            Graphics3::setTexture(texUnit0, textures[3]);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordX, TexGenDisabled);
            Graphics3::setTexCoordGeneration(texUnit0, TexCoordY, TexGenDisabled);
            Graphics3::setTextureMapping(texUnit0, Texture2D, true);
        }
        else
            Graphics3::setTextureMapping(texUnit0, Texture2D, false);
    }
    else
        Graphics3::setTextureMapping(texUnit0, Texture2D, false);

	// Setup Fog
	static float fogInterval;
	fogInterval += 1.0f;
	Graphics3::setRenderState(FogStart, 1.0f);
	Graphics3::setRenderState(FogEnd, ((Kore::cos(DEG_2_RAD(fogInterval)) + 1.0f) * 2.5f) + 2.0f);
	Graphics3::setRenderState(FogDensity, (Kore::cos(DEG_2_RAD(fogInterval * 0.5f)) + 1.0f) * 0.5f);
		
	Graphics3::setFogColor(Kore::Color(0xff808080));
	Graphics3::setRenderState(FogType, activeFogType);
	Graphics3::setRenderState(FogState, fogEnabled);

    // Setup scene greometry
    Graphics3::setViewMatrix(vMatrix.Invert());
    Graphics3::setWorldMatrix(wMatrix);

    MeshBuffer* meshBuf = meshBuffers[activeScene];
	Graphics3::setIndexBuffer(*meshBuf->indexBuffer);
	Graphics3::setVertexBuffer(*meshBuf->vertexBuffer);
	Graphics3::drawIndexedVertices();

	Graphics3::end();
	Graphics3::swapBuffers();
}

void onKeyEvent(KeyCode code, bool down) {
    //...
}

void keyDown(KeyCode code, wchar_t character)
{
    switch (code) {
        case Key_Escape:
            exit(0);
            break;

        case Key_Right:
            showNextScene();
            break;

        case Key_Left:
            showPrevScene();
            break;

		case Key_F:
			fogEnabled = !fogEnabled;
			break;
			
		case Key_F1:
			activeFogType = LinearFog;
			break;

		case Key_F2:
			activeFogType = ExpFog;
			break;

		case Key_F3:
			activeFogType = Exp2Fog;
			break;

        case Key_L:
            complexLightingEnabled = !complexLightingEnabled;
            break;
            
        case Key_R:
            rotationEnabled = !rotationEnabled;
            break;

        case Key_P:
            orthoProj = !orthoProj;
            updateProjection();
            break;

        case Key_T:
            textureMappingEnabled = !textureMappingEnabled;
            break;
    }

    onKeyEvent(code, true);
}

void keyUp(KeyCode code, wchar_t character)
{
    /*switch (code) {
        case Key_T:
            break;
    }*/

    onKeyEvent(code, false);
}

void mouseMove(int window, int x, int y, int movementX, int movementY) {
    //...
}
	
void mousePress(int window, int button, int x, int y) {
    //...
}

void mouseRelease(int window, int button, int x, int y) {
    //...
}


} // /namespace


#ifdef KOREC
extern "C"
#endif
int kore(int argc, char** argv)
{
    //Kore::Graphics3::setAntialiasingSamples(8);
    Kore::System::init("Test Environment", screenWidth, screenHeight);

    initScene();

    Kore::System::setCallback(onDrawFrame);

	//startTime = System::time();
	Kore::Mixer::init();
	Kore::Audio::init();

	Keyboard::the()->KeyDown = keyDown;
	Keyboard::the()->KeyUp = keyUp;
	Mouse::the()->Move = mouseMove;
	Mouse::the()->Press = mousePress;
	Mouse::the()->Release = mouseRelease;

	Kore::System::start();

    releaseScene();

    return 0;
}
