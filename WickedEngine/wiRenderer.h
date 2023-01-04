#pragma once
#include "CommonInclude.h"
#include "wiEnums.h"
#include "wiGraphicsDevice.h"
#include "wiScene.h"
#include "wiECS.h"
#include "wiPrimitive.h"
#include "wiCanvas.h"
#include "wiMath.h"
#include "shaders/ShaderInterop_Renderer.h"
#include "shaders/ShaderInterop_SurfelGI.h"
#include "wiVector.h"

#include <memory>
#include <limits>

namespace wi::renderer
{
	inline uint32_t CombineStencilrefs(wi::enums::STENCILREF engineStencilRef, uint8_t userStencilRef)
	{
		return (userStencilRef << 4) | static_cast<uint8_t>(engineStencilRef);
	}
	inline XMUINT3 GetEntityCullingTileCount(XMUINT2 internalResolution)
	{
		return XMUINT3(
			(internalResolution.x + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
			(internalResolution.y + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
			1
		);
	}
	inline XMUINT2 GetVisibilityTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE,
			(internalResolution.y + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE
		);
	}

	const wi::graphics::Sampler* GetSampler(wi::enums::SAMPLERTYPES id);
	const wi::graphics::Shader* GetShader(wi::enums::SHADERTYPE id);
	const wi::graphics::InputLayout* GetInputLayout(wi::enums::ILTYPES id);
	const wi::graphics::RasterizerState* GetRasterizerState(wi::enums::RSTYPES id);
	const wi::graphics::DepthStencilState* GetDepthStencilState(wi::enums::DSSTYPES id);
	const wi::graphics::BlendState* GetBlendState(wi::enums::BSTYPES id);
	const wi::graphics::GPUBuffer* GetConstantBuffer(wi::enums::CBTYPES id);
	const wi::graphics::Texture* GetTexture(wi::enums::TEXTYPES id);

	void ModifyObjectSampler(const wi::graphics::SamplerDesc& desc);

	// Initializes the renderer
	void Initialize();

	// Clears the scene and the associated renderer resources
	void ClearWorld(wi::scene::Scene& scene);

	// Returns the shader binary directory
	const std::string& GetShaderPath();
	// Sets the shader binary directory
	void SetShaderPath(const std::string& path);
	// Returns the shader source directory
	const std::string& GetShaderSourcePath();
	// Sets the shader source directory
	void SetShaderSourcePath(const std::string& path);
	// Reload shaders
	void ReloadShaders();
	// Returns how many shaders are embedded (if wiShaderDump.h is used)
	//	wiShaderDump.h can be generated by OfflineShaderCompiler.exe using shaderdump argument
	size_t GetShaderDumpCount();
	size_t GetShaderErrorCount();
	size_t GetShaderMissingCount();

	bool LoadShader(
		wi::graphics::ShaderStage stage,
		wi::graphics::Shader& shader,
		const std::string& filename,
		wi::graphics::ShaderModel minshadermodel = wi::graphics::ShaderModel::SM_6_0,
		const wi::vector<std::string>& permutation_defines = {}
	);


	struct Visibility
	{
		// User fills these:
		uint32_t layerMask = ~0u;
		const wi::scene::Scene* scene = nullptr;
		const wi::scene::CameraComponent* camera = nullptr;
		enum FLAGS
		{
			EMPTY = 0,
			ALLOW_OBJECTS = 1 << 0,
			ALLOW_LIGHTS = 1 << 1,
			ALLOW_DECALS = 1 << 2,
			ALLOW_ENVPROBES = 1 << 3,
			ALLOW_EMITTERS = 1 << 4,
			ALLOW_HAIRS = 1 << 5,
			ALLOW_REQUEST_REFLECTION = 1 << 6,
			ALLOW_OCCLUSION_CULLING = 1 << 7,

			ALLOW_EVERYTHING = ~0u
		};
		uint32_t flags = EMPTY;

		// wi::renderer::UpdateVisibility() fills these:
		wi::primitive::Frustum frustum;
		wi::vector<uint32_t> visibleObjects;
		wi::vector<uint32_t> visibleDecals;
		wi::vector<uint32_t> visibleEnvProbes;
		wi::vector<uint32_t> visibleEmitters;
		wi::vector<uint32_t> visibleHairs;
		wi::vector<uint32_t> visibleLights;

		std::atomic<uint32_t> object_counter;
		std::atomic<uint32_t> light_counter;
		std::atomic<uint32_t> decal_counter;

		wi::SpinLock locker;
		bool planar_reflection_visible = false;
		float closestRefPlane = std::numeric_limits<float>::max();
		XMFLOAT4 reflectionPlane = XMFLOAT4(0, 1, 0, 0);
		std::atomic_bool volumetriclight_request{ false };

		void Clear()
		{
			visibleObjects.clear();
			visibleLights.clear();
			visibleDecals.clear();
			visibleEnvProbes.clear();
			visibleEmitters.clear();
			visibleHairs.clear();

			object_counter.store(0);
			light_counter.store(0);
			decal_counter.store(0);

			closestRefPlane = std::numeric_limits<float>::max();
			planar_reflection_visible = false;
			volumetriclight_request.store(false);
		}

		bool IsRequestedPlanarReflections() const
		{
			return planar_reflection_visible;
		}
		bool IsRequestedVolumetricLights() const
		{
			return volumetriclight_request.load();
		}
	};

	// Performs frustum culling.
	void UpdateVisibility(Visibility& vis);
	// Prepares the scene for rendering
	void UpdatePerFrameData(
		wi::scene::Scene& scene,
		const Visibility& vis,
		FrameCB& frameCB,
		float dt
	);
	// Updates the GPU state according to the previously called UpdatePerFrameData()
	void UpdateRenderData(
		const Visibility& vis,
		const FrameCB& frameCB,
		wi::graphics::CommandList cmd
	);

	// Updates those GPU states that can be async
	void UpdateRenderDataAsync(
		const Visibility& vis,
		const FrameCB& frameCB,
		wi::graphics::CommandList cmd
	);

	void UpdateRaytracingAccelerationStructures(const wi::scene::Scene& scene, wi::graphics::CommandList cmd);

	// Binds all common constant buffers and samplers that may be used in all shaders
	void BindCommonResources(wi::graphics::CommandList cmd);
	// Updates the per camera constant buffer need to call for each different camera that is used when calling DrawScene() and the like
	//	camera_previous : camera from previous frame, used for reprojection effects.
	//	camera_reflection : camera that renders planar reflection
	void BindCameraCB(
		const wi::scene::CameraComponent& camera,
		const wi::scene::CameraComponent& camera_previous,
		const wi::scene::CameraComponent& camera_reflection,
		wi::graphics::CommandList cmd
	);


	enum DRAWSCENE_FLAGS
	{
		DRAWSCENE_OPAQUE = 1 << 0,
		DRAWSCENE_TRANSPARENT = 1 << 1,
		DRAWSCENE_OCCLUSIONCULLING = 1 << 2,
		DRAWSCENE_TESSELLATION = 1 << 3,
		DRAWSCENE_HAIRPARTICLE = 1 << 4,
		DRAWSCENE_IMPOSTOR = 1 << 5,
	};

	// Draw the world from a camera. You must call BindCameraCB() at least once in this frame prior to this
	void DrawScene(
		const Visibility& vis,
		wi::enums::RENDERPASS renderPass,
		wi::graphics::CommandList cmd,
		uint32_t flags = DRAWSCENE_OPAQUE
	);

	// Render mip levels for textures that reqested it:
	void ProcessDeferredMipGenRequests(wi::graphics::CommandList cmd);

	// Compute volumetric cloud shadow data
	void ComputeVolumetricCloudShadows(
		wi::graphics::CommandList cmd,
		const wi::graphics::Texture* weatherMapFirst = nullptr,
		const wi::graphics::Texture* weatherMapSecond = nullptr
	);
	// Compute essential atmospheric scattering textures for skybox, fog and clouds
	void ComputeAtmosphericScatteringTextures(wi::graphics::CommandList cmd);
	// Update atmospheric scattering primarily for environment probes.
	void RefreshAtmosphericScatteringTextures(wi::graphics::CommandList cmd);
	// Draw skydome centered to camera.
	void DrawSky(const wi::scene::Scene& scene, wi::graphics::CommandList cmd);
	// Draw shadow maps for each visible light that has associated shadow maps
	void DrawSun(wi::graphics::CommandList cmd);
	// Draw shadow maps for each visible light that has associated shadow maps
	void DrawShadowmaps(
		const Visibility& vis,
		wi::graphics::CommandList cmd
	);
	// Draw debug world. You must also enable what parts to draw, eg. SetToDrawGridHelper, etc, see implementation for details what can be enabled.
	void DrawDebugWorld(
		const wi::scene::Scene& scene,
		const wi::scene::CameraComponent& camera,
		const wi::Canvas& canvas,
		wi::graphics::CommandList cmd
	);
	// Draw Soft offscreen particles.
	void DrawSoftParticles(
		const Visibility& vis,
		const wi::graphics::Texture& lineardepth,
		bool distortion, 
		wi::graphics::CommandList cmd
	);
	// Draw simple light visualizer geometries
	void DrawLightVisualizers(
		const Visibility& vis,
		wi::graphics::CommandList cmd
	);
	// Draw volumetric light scattering effects
	void DrawVolumeLights(
		const Visibility& vis,
		wi::graphics::CommandList cmd
	);
	// Draw Lens Flares for lights that have them enabled
	void DrawLensFlares(
		const Visibility& vis,
		wi::graphics::CommandList cmd,
		const wi::graphics::Texture* texture_directional_occlusion = nullptr
	);
	// Call once per frame to re-render out of date environment probes
	void RefreshEnvProbes(const Visibility& vis, wi::graphics::CommandList cmd);
	// Call once per frame to re-render out of date impostors
	void RefreshImpostors(const wi::scene::Scene& scene, wi::graphics::CommandList cmd);
	// Call once per frame to repack out of date lightmaps in the atlas
	void RefreshLightmaps(const wi::scene::Scene& scene, wi::graphics::CommandList cmd, uint8_t instanceInclusionMask = 0xFF);
	// Run a compute shader that will resolve a MSAA depth buffer to a single-sample texture
	void ResolveMSAADepthBuffer(const wi::graphics::Texture& dst, const wi::graphics::Texture& src, wi::graphics::CommandList cmd);
	void DownsampleDepthBuffer(const wi::graphics::Texture& src, wi::graphics::CommandList cmd);

	struct TiledLightResources
	{
		XMUINT3 tileCount = {};
		wi::graphics::GPUBuffer tileFrustums; // entity culling frustums
		wi::graphics::GPUBuffer entityTiles_Opaque; // culled entity indices (for opaque pass)
		wi::graphics::GPUBuffer entityTiles_Transparent; // culled entity indices (for transparent pass)
	};
	void CreateTiledLightResources(TiledLightResources& res, XMUINT2 resolution);
	// Compute light grid tiles
	void ComputeTiledLightCulling(
		const TiledLightResources& res,
		const wi::graphics::Texture& debugUAV,
		wi::graphics::CommandList cmd
	);

	struct LuminanceResources
	{
		wi::graphics::GPUBuffer luminance;
	};
	void CreateLuminanceResources(LuminanceResources& res, XMUINT2 resolution);
	// Compute the luminance for the source image and return the texture containing the luminance value in pixel [0,0]
	void ComputeLuminance(
		const LuminanceResources& res,
		const wi::graphics::Texture& sourceImage,
		wi::graphics::CommandList cmd,
		float adaption_rate = 1,
		float eyeadaptionkey = 0.115f
	);

	struct BloomResources
	{
		wi::graphics::Texture texture_bloom;
		wi::graphics::Texture texture_temp;
	};
	void CreateBloomResources(BloomResources& res, XMUINT2 resolution);
	void ComputeBloom(
		const BloomResources& res,
		const wi::graphics::Texture& input,
		wi::graphics::CommandList cmd,
		float threshold = 1.0f, // cutoff value, pixels below this will not contribute to bloom
		float exposure = 1.0f,
		const wi::graphics::GPUBuffer* buffer_luminance = nullptr
	);

	void ComputeShadingRateClassification(
		const wi::graphics::Texture& output,
		const wi::graphics::Texture& debugUAV,
		wi::graphics::CommandList cmd
	);

	struct VisibilityResources
	{
		XMUINT2 tile_count = {};
		wi::graphics::GPUBuffer bins;
		wi::graphics::GPUBuffer binned_tiles;
		wi::graphics::Texture texture_payload_0;
		wi::graphics::Texture texture_payload_1;
		wi::graphics::Texture texture_normals;
		wi::graphics::Texture texture_roughness;

		// You can request any of these extra outputs to be written by VisibilityResolve:
		const wi::graphics::Texture* depthbuffer = nullptr; // depth buffer that matches with post projection
		const wi::graphics::Texture* lineardepth = nullptr; // depth buffer in linear space in [0,1] range
		const wi::graphics::Texture* primitiveID_resolved = nullptr; // resolved from MSAA texture_visibility input
	};
	void CreateVisibilityResources(VisibilityResources& res, XMUINT2 resolution);
	void Visibility_Prepare(
		const VisibilityResources& res,
		const wi::graphics::Texture& input_primitiveID, // can be MSAA
		wi::graphics::CommandList cmd
	);
	void Visibility_Surface(
		const VisibilityResources& res,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);
	void Visibility_Surface_Reduced(
		const VisibilityResources& res,
		wi::graphics::CommandList cmd
	);
	void Visibility_Shade(
		const VisibilityResources& res,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);
	void Visibility_Velocity(
		const VisibilityResources& res,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);

	// Surfel GI: diffuse GI with ray tracing from surfels
	struct SurfelGIResources
	{
		wi::graphics::Texture result;
	};
	void CreateSurfelGIResources(SurfelGIResources& res, XMUINT2 resolution);
	void SurfelGI_Coverage(
		const SurfelGIResources& res,
		const wi::scene::Scene& scene,
		const wi::graphics::Texture& debugUAV,
		wi::graphics::CommandList cmd
	);
	void SurfelGI(
		const SurfelGIResources& res,
		const wi::scene::Scene& scene,
		wi::graphics::CommandList cmd,
		uint8_t instanceInclusionMask = 0xFF
	);

	// DDGI: Dynamic Diffuse Global Illumination (probe-based ray tracing)
	void DDGI(
		const wi::scene::Scene& scene,
		wi::graphics::CommandList cmd,
		uint8_t instanceInclusionMask = 0xFF
	);

	// VXGI: Voxel-based Global Illumination (voxel cone tracing-based)
	struct VXGIResources
	{
		wi::graphics::Texture diffuse[2];
		wi::graphics::Texture specular[2];
		mutable bool pre_clear = true;

		bool IsValid() const { return diffuse[0].IsValid(); }
	};
	void CreateVXGIResources(VXGIResources& res, XMUINT2 resolution);
	void VXGI_Voxelize(
		const Visibility& vis,
		wi::graphics::CommandList cmd
	);
	void VXGI_Resolve(
		const VXGIResources& res,
		const wi::scene::Scene& scene,
		wi::graphics::Texture texture_lineardepth,
		wi::graphics::CommandList cmd
	);

	void Postprocess_Blur_Gaussian(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& temp,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		int mip_src = -1,
		int mip_dst = -1,
		bool wide = false
	);
	void Postprocess_Blur_Bilateral(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& lineardepth,
		const wi::graphics::Texture& temp,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float depth_threshold = 1.0f,
		int mip_src = -1,
		int mip_dst = -1,
		bool wide = false
	);
	struct SSAOResources
	{
		wi::graphics::Texture temp;
	};
	void CreateSSAOResources(SSAOResources& res, XMUINT2 resolution);
	void Postprocess_SSAO(
		const SSAOResources& res,
		const wi::graphics::Texture& output,
		const wi::graphics::Texture& lineardepth,
		wi::graphics::CommandList cmd,
		float range = 1.0f,
		uint32_t samplecount = 16,
		float power = 1.0f
	);
	void Postprocess_HBAO(
		const SSAOResources& res,
		const wi::scene::CameraComponent& camera,
		const wi::graphics::Texture& lineardepth,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float power = 1.0f
		);
	struct MSAOResources
	{
		wi::graphics::Texture texture_lineardepth_downsize1;
		wi::graphics::Texture texture_lineardepth_tiled1;
		wi::graphics::Texture texture_lineardepth_downsize2;
		wi::graphics::Texture texture_lineardepth_tiled2;
		wi::graphics::Texture texture_lineardepth_downsize3;
		wi::graphics::Texture texture_lineardepth_tiled3;
		wi::graphics::Texture texture_lineardepth_downsize4;
		wi::graphics::Texture texture_lineardepth_tiled4;
		wi::graphics::Texture texture_ao_merged1;
		wi::graphics::Texture texture_ao_hq1;
		wi::graphics::Texture texture_ao_smooth1;
		wi::graphics::Texture texture_ao_merged2;
		wi::graphics::Texture texture_ao_hq2;
		wi::graphics::Texture texture_ao_smooth2;
		wi::graphics::Texture texture_ao_merged3;
		wi::graphics::Texture texture_ao_hq3;
		wi::graphics::Texture texture_ao_smooth3;
		wi::graphics::Texture texture_ao_merged4;
		wi::graphics::Texture texture_ao_hq4;
	};
	void CreateMSAOResources(MSAOResources& res, XMUINT2 resolution);
	void Postprocess_MSAO(
		const MSAOResources& res,
		const wi::scene::CameraComponent& camera,
		const wi::graphics::Texture& lineardepth,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float power = 1.0f
		);
	struct RTAOResources
	{
		wi::graphics::Texture normals;

		mutable int frame = 0;
		wi::graphics::GPUBuffer tiles;
		wi::graphics::GPUBuffer metadata;
		wi::graphics::Texture scratch[2];
		wi::graphics::Texture moments[2];
	};
	void CreateRTAOResources(RTAOResources& res, XMUINT2 resolution);
	void Postprocess_RTAO(
		const RTAOResources& res,
		const wi::scene::Scene& scene,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float range = 1.0f,
		float power = 1.0f,
		uint8_t instanceInclusionMask = 0xFF
	);
	struct RTDiffuseResources
	{
		mutable int frame = 0;
		wi::graphics::Texture texture_rayIndirectDiffuse;
		wi::graphics::Texture texture_spatial;
		wi::graphics::Texture texture_spatial_variance;
		wi::graphics::Texture texture_temporal[2];
		wi::graphics::Texture texture_temporal_variance[2];
		wi::graphics::Texture texture_bilateral_temp;
	};
	void CreateRTDiffuseResources(RTDiffuseResources& res, XMUINT2 resolution);
	void Postprocess_RTDiffuse(
		const RTDiffuseResources& res,
		const wi::scene::Scene& scene,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float range = 1000.0f,
		uint8_t instanceInclusionMask = 0xFF
	);
	struct RTReflectionResources
	{
		mutable int frame = 0;
		wi::graphics::Texture texture_rayIndirectSpecular;
		wi::graphics::Texture texture_rayDirectionPDF;
		wi::graphics::Texture texture_rayLengths;
		wi::graphics::Texture texture_resolve;
		wi::graphics::Texture texture_resolve_variance;
		wi::graphics::Texture texture_resolve_reprojectionDepth;
		wi::graphics::Texture texture_temporal[2];
		wi::graphics::Texture texture_temporal_variance[2];
		wi::graphics::Texture texture_bilateral_temp;
	};
	void CreateRTReflectionResources(RTReflectionResources& res, XMUINT2 resolution);
	void Postprocess_RTReflection(
		const RTReflectionResources& res,
		const wi::scene::Scene& scene,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float range = 1000.0f,
		float roughnessCutoff = 0.5f,
		uint8_t instanceInclusionMask = 0xFF
	);
	struct SSRResources
	{
		mutable int frame = 0;
		wi::graphics::Texture texture_tile_minmax_roughness_horizontal;
		wi::graphics::Texture texture_tile_minmax_roughness;
		wi::graphics::Texture texture_depth_hierarchy;
		wi::graphics::Texture texture_rayIndirectSpecular;
		wi::graphics::Texture texture_rayDirectionPDF;
		wi::graphics::Texture texture_rayLengths;
		wi::graphics::Texture texture_resolve;
		wi::graphics::Texture texture_resolve_variance;
		wi::graphics::Texture texture_resolve_reprojectionDepth;
		wi::graphics::Texture texture_temporal[2];
		wi::graphics::Texture texture_temporal_variance[2];
		wi::graphics::Texture texture_bilateral_temp;
		wi::graphics::GPUBuffer buffer_tile_tracing_statistics;
		wi::graphics::GPUBuffer buffer_tiles_tracing_earlyexit;
		wi::graphics::GPUBuffer buffer_tiles_tracing_cheap;
		wi::graphics::GPUBuffer buffer_tiles_tracing_expensive;
	};
	void CreateSSRResources(SSRResources& res, XMUINT2 resolution);
	void Postprocess_SSR(
		const SSRResources& res,
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float roughnessCutoff = 0.6f
	);
	struct RTShadowResources
	{
		wi::graphics::Texture temp;
		wi::graphics::Texture temporal[2];
		wi::graphics::Texture normals;

		mutable int frame = 0;
		wi::graphics::GPUBuffer tiles;
		wi::graphics::GPUBuffer metadata;
		wi::graphics::Texture scratch[4][2];
		wi::graphics::Texture moments[4][2];
		wi::graphics::Texture denoised;
	};
	void CreateRTShadowResources(RTShadowResources& res, XMUINT2 resolution);
	void Postprocess_RTShadow(
		const RTShadowResources& res,
		const wi::scene::Scene& scene,
		const wi::graphics::GPUBuffer& entityTiles_Opaque,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		uint8_t instanceInclusionMask = 0xFF
	);
	struct ScreenSpaceShadowResources
	{
		int placeholder;
	};
	void CreateScreenSpaceShadowResources(ScreenSpaceShadowResources& res, XMUINT2 resolution);
	void Postprocess_ScreenSpaceShadow(
		const ScreenSpaceShadowResources& res,
		const wi::graphics::GPUBuffer& entityTiles_Opaque,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float range = 1,
		uint32_t samplecount = 16
	);
	void Postprocess_LightShafts(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		const XMFLOAT2& center,
		float strength = 0.1f
	);
	struct DepthOfFieldResources
	{
		wi::graphics::Texture texture_tilemax_horizontal;
		wi::graphics::Texture texture_tilemin_horizontal;
		wi::graphics::Texture texture_tilemax;
		wi::graphics::Texture texture_tilemin;
		wi::graphics::Texture texture_neighborhoodmax;
		wi::graphics::Texture texture_presort;
		wi::graphics::Texture texture_prefilter;
		wi::graphics::Texture texture_main;
		wi::graphics::Texture texture_postfilter;
		wi::graphics::Texture texture_alpha1;
		wi::graphics::Texture texture_alpha2;
		wi::graphics::GPUBuffer buffer_tile_statistics;
		wi::graphics::GPUBuffer buffer_tiles_earlyexit;
		wi::graphics::GPUBuffer buffer_tiles_cheap;
		wi::graphics::GPUBuffer buffer_tiles_expensive;

		bool IsValid() const { return texture_tilemax_horizontal.IsValid(); }
	};
	void CreateDepthOfFieldResources(DepthOfFieldResources& res, XMUINT2 resolution);
	void Postprocess_DepthOfField(
		const DepthOfFieldResources& res,
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float coc_scale = 10,
		float max_coc = 18
	);
	void Postprocess_Outline(
		const wi::graphics::Texture& input,
		wi::graphics::CommandList cmd,
		float threshold = 0.1f,
		float thickness = 1.0f,
		const XMFLOAT4& color = XMFLOAT4(0, 0, 0, 1)
	);
	struct MotionBlurResources
	{
		wi::graphics::Texture texture_tilemin_horizontal;
		wi::graphics::Texture texture_tilemax_horizontal;
		wi::graphics::Texture texture_tilemax;
		wi::graphics::Texture texture_tilemin;
		wi::graphics::Texture texture_neighborhoodmax;
		wi::graphics::GPUBuffer buffer_tile_statistics;
		wi::graphics::GPUBuffer buffer_tiles_earlyexit;
		wi::graphics::GPUBuffer buffer_tiles_cheap;
		wi::graphics::GPUBuffer buffer_tiles_expensive;

		bool IsValid() const { return texture_tilemax_horizontal.IsValid(); }
	};
	void CreateMotionBlurResources(MotionBlurResources& res, XMUINT2 resolution);
	void Postprocess_MotionBlur(
		const MotionBlurResources& res,
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float strength = 100.0f
	);
	struct VolumetricCloudResources
	{
		mutable int frame = 0;
		wi::graphics::Texture texture_cloudRender;
		wi::graphics::Texture texture_cloudDepth;
		wi::graphics::Texture texture_reproject[2];
		wi::graphics::Texture texture_reproject_depth[2];
		wi::graphics::Texture texture_reproject_additional[2];
		wi::graphics::Texture texture_cloudMask;
	};
	void CreateVolumetricCloudResources(VolumetricCloudResources& res, XMUINT2 resolution);
	void Postprocess_VolumetricClouds(
		const VolumetricCloudResources& res,
		wi::graphics::CommandList cmd,
		const wi::scene::CameraComponent& camera,
		const wi::scene::CameraComponent& camera_previous,
		const wi::scene::CameraComponent& camera_reflection,
		const bool jitterEnabled,
		const wi::graphics::Texture* weatherMapFirst = nullptr,
		const wi::graphics::Texture* weatherMapSecond = nullptr
	);
	void Postprocess_FXAA(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);
	struct TemporalAAResources
	{
		mutable int frame = 0;
		wi::graphics::Texture texture_temporal[2];

		bool IsValid() const { return texture_temporal[0].IsValid(); }
		const wi::graphics::Texture* GetCurrent() const { return &texture_temporal[frame % arraysize(texture_temporal)]; }
		const wi::graphics::Texture* GetHistory() const { return &texture_temporal[(frame + 1) % arraysize(texture_temporal)]; }
	};
	void CreateTemporalAAResources(TemporalAAResources& res, XMUINT2 resolution);
	void Postprocess_TemporalAA(
		const TemporalAAResources& res,
		const wi::graphics::Texture& input,
		wi::graphics::CommandList cmd
	);
	void Postprocess_Sharpen(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float amount = 1.0f
	);
	void Postprocess_Tonemap(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float exposure,
		float brightness,
		float contrast,
		float saturation,
		bool dither,
		const wi::graphics::Texture* texture_colorgradinglut = nullptr,
		const wi::graphics::Texture* texture_distortion = nullptr,
		const wi::graphics::GPUBuffer* buffer_luminance = nullptr,
		const wi::graphics::Texture* texture_bloom = nullptr,
		wi::graphics::ColorSpace display_colorspace = wi::graphics::ColorSpace::SRGB
	);
	void Postprocess_FSR(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& temp,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float sharpness = 1.0f
	);
	struct FSR2Resources
	{
		struct Fsr2Constants
		{
			int32_t   renderSize[2];
			int32_t   displaySize[2];
			uint32_t  lumaMipDimensions[2];
			uint32_t  lumaMipLevelToUse;
			uint32_t  frameIndex;
			float     displaySizeRcp[2];
			float     jitterOffset[2];
			float     deviceToViewDepth[4];
			float     depthClipUVScale[2];
			float     postLockStatusUVScale[2];
			float     reactiveMaskDimRcp[2];
			float     motionVectorScale[2];
			float     downscaleFactor[2];
			float     preExposure;
			float     tanHalfFOV;
			float     motionVectorJitterCancellation[2];
			float     jitterPhaseCount;
			float     lockInitialLifetime;
			float     lockTickDelta;
			float     deltaTime;
			float     dynamicResChangeFactor;
			float     lumaMipRcp;
		};
		mutable Fsr2Constants fsr2_constants = {};
		wi::graphics::Texture adjusted_color;
		wi::graphics::Texture luminance_current;
		wi::graphics::Texture luminance_history;
		wi::graphics::Texture exposure;
		wi::graphics::Texture previous_depth;
		wi::graphics::Texture dilated_depth;
		wi::graphics::Texture dilated_motion;
		wi::graphics::Texture dilated_reactive;
		wi::graphics::Texture disocclusion_mask;
		wi::graphics::Texture lock_status[2];
		wi::graphics::Texture reactive_mask;
		wi::graphics::Texture lanczos_lut;
		wi::graphics::Texture maximum_bias_lut;
		wi::graphics::Texture spd_global_atomic;
		wi::graphics::Texture output_internal[2];

		bool IsValid() const { return adjusted_color.IsValid(); }

		XMFLOAT2 GetJitter() const;
	};
	void CreateFSR2Resources(FSR2Resources& res, XMUINT2 render_resolution, XMUINT2 presentation_resolution);
	void Postprocess_FSR2(
		const FSR2Resources& res,
		const wi::scene::CameraComponent& camera,
		const wi::graphics::Texture& input_pre_alpha,
		const wi::graphics::Texture& input_post_alpha,
		const wi::graphics::Texture& input_depth,
		const wi::graphics::Texture& input_velocity,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float dt, // delta time in seconds
		float sharpness = 0.5f
	);
	void Postprocess_Chromatic_Aberration(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		float amount = 1.0f
	);
	void Postprocess_Upsample_Bilateral(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& lineardepth,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd,
		bool is_pixelshader = false,
		float threshold = 1.0f
	);
	void Postprocess_Downsample4x(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);
	void Postprocess_NormalsFromDepth(
		const wi::graphics::Texture& depthbuffer,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);
	void Postprocess_Underwater(
		const wi::graphics::Texture& input,
		const wi::graphics::Texture& output,
		wi::graphics::CommandList cmd
	);

	// Render the scene with ray tracing
	void RayTraceScene(
		const wi::scene::Scene& scene,
		const wi::graphics::Texture& output,
		int accumulation_sample,
		wi::graphics::CommandList cmd,
		uint8_t instanceInclusionMask = 0xFF,
		const wi::graphics::Texture* output_albedo = nullptr,
		const wi::graphics::Texture* output_normal = nullptr
	);
	// Render the scene BVH with ray tracing to the screen
	void RayTraceSceneBVH(const wi::scene::Scene& scene, wi::graphics::CommandList cmd);

	// Render occluders against a depth buffer
	void OcclusionCulling_Reset(const Visibility& vis, wi::graphics::CommandList cmd);
	void OcclusionCulling_Render(const wi::scene::CameraComponent& camera, const Visibility& vis, wi::graphics::CommandList cmd);
	void OcclusionCulling_Resolve(const Visibility& vis, wi::graphics::CommandList cmd);


	enum MIPGENFILTER
	{
		MIPGENFILTER_POINT,
		MIPGENFILTER_LINEAR,
		MIPGENFILTER_GAUSSIAN,
	};
	struct MIPGEN_OPTIONS
	{
		int arrayIndex = -1;
		const wi::graphics::Texture* gaussian_temp = nullptr;
		bool preserve_coverage = false;
		bool wide_gauss = false;
	};
	void GenerateMipChain(const wi::graphics::Texture& texture, MIPGENFILTER filter, wi::graphics::CommandList cmd, const MIPGEN_OPTIONS& options = {});

	enum BORDEREXPANDSTYLE
	{
		BORDEREXPAND_DISABLE,
		BORDEREXPAND_WRAP,
		BORDEREXPAND_CLAMP,
	};
	// Performs copy operation even between different texture formats
	//	NOTE: DstMIP can be specified as -1 to use main subresource, otherwise the subresource (>=0) must have been generated explicitly!
	//	Can also expand border region according to desired sampler func
	void CopyTexture2D(
		const wi::graphics::Texture& dst, int DstMIP, int DstX, int DstY,
		const wi::graphics::Texture& src, int SrcMIP, 
		wi::graphics::CommandList cmd,
		BORDEREXPANDSTYLE borderExpand = BORDEREXPAND_DISABLE
	);

	void DrawWaterRipples(const Visibility& vis, wi::graphics::CommandList cmd);



	void SetShadowProps2D(int max_resolution);
	void SetShadowPropsCube(int max_resolution);



	void SetTransparentShadowsEnabled(float value);
	float GetTransparentShadowsEnabled();
	void SetWireRender(bool value);
	bool IsWireRender();
	void SetToDrawDebugBoneLines(bool param);
	bool GetToDrawDebugBoneLines();
	void SetToDrawDebugPartitionTree(bool param);
	bool GetToDrawDebugPartitionTree();
	bool GetToDrawDebugEnvProbes();
	void SetToDrawDebugEnvProbes(bool value);
	void SetToDrawDebugEmitters(bool param);
	bool GetToDrawDebugEmitters();
	void SetToDrawDebugForceFields(bool param);
	bool GetToDrawDebugForceFields();
	void SetToDrawDebugCameras(bool param);
	bool GetToDrawDebugCameras();
	void SetToDrawDebugColliders(bool param);
	bool GetToDrawDebugColliders();
	bool GetToDrawGridHelper();
	void SetToDrawGridHelper(bool value);
	bool GetToDrawVoxelHelper();
	void SetToDrawVoxelHelper(bool value, int clipmap_level);
	void SetDebugLightCulling(bool enabled);
	bool GetDebugLightCulling();
	void SetAdvancedLightCulling(bool enabled);
	bool GetAdvancedLightCulling();
	void SetVariableRateShadingClassification(bool enabled);
	bool GetVariableRateShadingClassification();
	void SetVariableRateShadingClassificationDebug(bool enabled);
	bool GetVariableRateShadingClassificationDebug();
	void SetOcclusionCullingEnabled(bool enabled);
	bool GetOcclusionCullingEnabled();
	void SetTemporalAAEnabled(bool enabled);
	bool GetTemporalAAEnabled();
	void SetTemporalAADebugEnabled(bool enabled);
	bool GetTemporalAADebugEnabled();
	void SetFreezeCullingCameraEnabled(bool enabled);
	bool GetFreezeCullingCameraEnabled();
	void SetVXGIEnabled(bool enabled);
	bool GetVXGIEnabled();
	void SetVXGIReflectionsEnabled(bool enabled);
	bool GetVXGIReflectionsEnabled();
	void SetGameSpeed(float value);
	float GetGameSpeed();
	void SetRaytraceBounceCount(uint32_t bounces);
	uint32_t GetRaytraceBounceCount();
	void SetRaytraceDebugBVHVisualizerEnabled(bool value);
	bool GetRaytraceDebugBVHVisualizerEnabled();
	void SetRaytracedShadowsEnabled(bool value);
	bool GetRaytracedShadowsEnabled();
	void SetTessellationEnabled(bool value);
	bool GetTessellationEnabled();
	void SetDisableAlbedoMaps(bool value);
	bool IsDisableAlbedoMaps();
	void SetForceDiffuseLighting(bool value);
	bool IsForceDiffuseLighting();
	void SetScreenSpaceShadowsEnabled(bool value);
	bool GetScreenSpaceShadowsEnabled();
	void SetSurfelGIEnabled(bool value);
	bool GetSurfelGIEnabled();
	void SetSurfelGIDebugEnabled(SURFEL_DEBUG value);
	SURFEL_DEBUG GetSurfelGIDebugEnabled();
	void SetDDGIEnabled(bool value);
	bool GetDDGIEnabled();
	void SetDDGIDebugEnabled(bool value);
	bool GetDDGIDebugEnabled();
	void SetDDGIRayCount(uint32_t value);
	uint32_t GetDDGIRayCount();
	void SetDDGIBlendSpeed(float value);
	float GetDDGIBlendSpeed();
	void SetGIBoost(float value);
	float GetGIBoost();
	void Workaround( const int bug, wi::graphics::CommandList cmd);

	// Gets pick ray according to the current screen resolution and pointer coordinates. Can be used as input into RayIntersectWorld()
	wi::primitive::Ray GetPickRay(long cursorX, long cursorY, const wi::Canvas& canvas, const wi::scene::CameraComponent& camera = wi::scene::GetCamera());


	// Add box to render in next frame. It will be rendered in DrawDebugWorld()
	void DrawBox(const XMFLOAT4X4& boxMatrix, const XMFLOAT4& color = XMFLOAT4(1,1,1,1));
	// Add sphere to render in next frame. It will be rendered in DrawDebugWorld()
	void DrawSphere(const wi::primitive::Sphere& sphere, const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1));
	// Add capsule to render in next frame. It will be rendered in DrawDebugWorld()
	void DrawCapsule(const wi::primitive::Capsule& capsule, const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1));

	struct RenderableLine
	{
		XMFLOAT3 start = XMFLOAT3(0, 0, 0);
		XMFLOAT3 end = XMFLOAT3(0, 0, 0);
		XMFLOAT4 color_start = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 color_end = XMFLOAT4(1, 1, 1, 1);
	};
	// Add line to render in the next frame. It will be rendered in DrawDebugWorld()
	void DrawLine(const RenderableLine& line);

	struct RenderableLine2D
	{
		XMFLOAT2 start = XMFLOAT2(0, 0);
		XMFLOAT2 end = XMFLOAT2(0, 0);
		XMFLOAT4 color_start = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 color_end = XMFLOAT4(1, 1, 1, 1);
	};
	// Add 2D line to render in the next frame. It will be rendered in DrawDebugWorld() in screen space
	void DrawLine(const RenderableLine2D& line);

	struct RenderablePoint
	{
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		float size = 1.0f;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
	};
	// Add point to render in the next frame. It will be rendered in DrawDebugWorld() as an X
	void DrawPoint(const RenderablePoint& point);

	struct RenderableTriangle
	{
		XMFLOAT3 positionA = XMFLOAT3(0, 0, 0);
		XMFLOAT4 colorA = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT3 positionB = XMFLOAT3(0, 0, 0);
		XMFLOAT4 colorB = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT3 positionC = XMFLOAT3(0, 0, 0);
		XMFLOAT4 colorC = XMFLOAT4(1, 1, 1, 1);
	};
	// Add triangle to render in the next frame. It will be rendered in DrawDebugWorld()
	void DrawTriangle(const RenderableTriangle& triangle, bool wireframe = false);

	struct DebugTextParams
	{
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		int pixel_height = 32;
		float scaling = 1;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
		enum FLAGS // do not change values, it's bound to lua manually!
		{
			NONE = 0,
			DEPTH_TEST = 1 << 0,		// text can be occluded by geometry
			CAMERA_FACING = 1 << 1,		// text will be rotated to face the camera
			CAMERA_SCALING = 1 << 2,	// text will be always the same size, independent of distance to camera
		};
		uint32_t flags = NONE;
	};
	// Add text to render in the next frame. It will be rendered in DrawDebugWorld()
	//	The memory to text doesn't need to be retained by the caller, as it will be copied internally
	void DrawDebugText(const char* text, const DebugTextParams& params);

	struct PaintRadius
	{
		wi::ecs::Entity objectEntity = wi::ecs::INVALID_ENTITY;
		int subset = -1;
		uint32_t uvset = 0;
		float radius = 0;
		XMUINT2 center = {};
		XMUINT2 dimensions = {};
		float rotation = 0;
		uint shape = 0; // 0: circle, 1 : square
	};
	void DrawPaintRadius(const PaintRadius& paintrad);

	// Add a texture that should be mipmapped whenever it is feasible to do so
	void AddDeferredMIPGen(const wi::graphics::Texture& texture, bool preserve_coverage = false);

	struct CustomShader
	{
		std::string name;
		uint32_t filterMask = wi::enums::FILTER_OPAQUE;
		wi::graphics::PipelineState pso[wi::enums::RENDERPASS_COUNT] = {};
	};
	// Registers a custom shader that can be set to materials. 
	//	Returns the ID of the custom shader that can be used with MaterialComponent::SetCustomShaderID()
	int RegisterCustomShader(const CustomShader& customShader);
	const wi::vector<CustomShader>& GetCustomShaders();

};

