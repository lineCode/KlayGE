#include <KlayGE/KlayGE.hpp>
#include <KlayGE/ThrowErr.hpp>
#include <KlayGE/Util.hpp>
#include <KlayGE/Math.hpp>
#include <KlayGE/Font.hpp>
#include <KlayGE/Renderable.hpp>
#include <KlayGE/RenderableHelper.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/FrameBuffer.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/RenderSettings.hpp>
#include <KlayGE/KMesh.hpp>
#include <KlayGE/SceneObjectHelper.hpp>
#include <KlayGE/PostProcess.hpp>
#include <KlayGE/HDRPostProcess.hpp>

#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/InputFactory.hpp>

#include <sstream>
#include <ctime>
#include <boost/bind.hpp>
#include <boost/typeof/typeof.hpp>
#ifdef KLAYGE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable: 4702)
#endif
#include <boost/lexical_cast.hpp>
#ifdef KLAYGE_COMPILER_MSVC
#pragma warning(pop)
#endif

#include "DeferredShadingLayer.hpp"
#include "DeferredShading.hpp"

using namespace std;
using namespace KlayGE;

namespace
{
	class RenderTorus : public KMesh
	{
	public:
		RenderTorus(RenderModelPtr const & model, std::wstring const & name)
			: KMesh(model, name),
				gen_sm_pass_(false), emit_pass_(false)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			effect_ = rf.LoadEffect("GBuffer.fxml");

			mvp_param_ = effect_->ParameterByName("mvp");
			model_view_param_ = effect_->ParameterByName("model_view");
			depth_near_far_invfar_param_ = effect_->ParameterByName("depth_near_far_invfar");
		}

		void BuildMeshInfo()
		{
			std::map<std::string, TexturePtr> tex_pool;

			RenderModel::Material const & mtl = model_.lock()->GetMaterial(this->MaterialID());

			bool has_diffuse_map = false;
			bool has_bump_map = false;
			RenderModel::TextureSlotsType const & texture_slots = mtl.texture_slots;
			for (RenderModel::TextureSlotsType::const_iterator iter = texture_slots.begin();
				iter != texture_slots.end(); ++ iter)
			{
				TexturePtr tex;
				BOOST_AUTO(titer, tex_pool.find(iter->second));
				if (titer != tex_pool.end())
				{
					tex = titer->second;
				}
				else
				{
					tex = LoadTexture(iter->second, EAH_GPU_Read)();
					tex_pool.insert(std::make_pair(iter->second, tex));
				}
				has_diffuse_map = tex;

				if ("Diffuse Color" == iter->first)
				{
					*(effect_->ParameterByName("diffuse_tex")) = tex;
				}
				if ("Bump" == iter->first)
				{
					*(effect_->ParameterByName("bump_tex")) = tex;
					if (tex)
					{
						has_bump_map = true;
					}
				}
			}

			is_emit_ = ((mtl.emit.x() != 0) || (mtl.emit.y() != 0) || (mtl.emit.z() != 0));

			*(effect_->ParameterByName("diffuse_clr")) = float4(mtl.diffuse.x(), mtl.diffuse.y(), mtl.diffuse.z(), 1);
			*(effect_->ParameterByName("emit_clr")) = float4(mtl.emit.x(), mtl.emit.y(), mtl.emit.z(), 1);
			*(effect_->ParameterByName("specular_level")) = mtl.specular_level;
			*(effect_->ParameterByName("shininess")) = MathLib::clamp(mtl.shininess / 256.0f, 0.0f, 1.0f);

			if (has_diffuse_map)
			{
				if (has_bump_map)
				{
					gbuffer_technique_ = effect_->TechniqueByName("GBufferDiffBumpTech");
				}
				else
				{
					gbuffer_technique_ = effect_->TechniqueByName("GBufferDiffTech");
				}
			}
			else
			{
				if (has_bump_map)
				{
					gbuffer_technique_ = effect_->TechniqueByName("GBufferBumpTech");
				}
				else
				{
					gbuffer_technique_ = effect_->TechniqueByName("GBufferNoTexTech");
				}
			}

			gen_sm_technique_ = effect_->TechniqueByName("GenShadowMap");
			emit_technique_ = effect_->TechniqueByName("EmitOnly");
		}

		void GenShadowMapPass(bool sm_pass)
		{
			gen_sm_pass_ = sm_pass;
			if (gen_sm_pass_)
			{
				technique_ = gen_sm_technique_;
			}
			else
			{
				technique_ = gbuffer_technique_;
			}
		}

		void EmitPass(bool emit_pass)
		{
			emit_pass_ = emit_pass;
			if (emit_pass_)
			{
				technique_ = emit_technique_;
			}
			else
			{
				technique_ = gbuffer_technique_;
			}
		}

		bool IsEmit() const
		{
			return is_emit_;
		}

		void OnRenderBegin()
		{
			Camera const & camera = Context::Instance().AppInstance().ActiveCamera();

			float4x4 const & view = camera.ViewMatrix();
			float4x4 const & proj = camera.ProjMatrix();

			*mvp_param_ = view * proj;
			*model_view_param_ = view;

			*depth_near_far_invfar_param_ = float3(camera.NearPlane(), camera.FarPlane(), 1 / camera.FarPlane());
		}

	private:
		bool is_emit_;

		KlayGE::RenderEffectPtr effect_;
		bool gen_sm_pass_;
		bool emit_pass_;
		RenderTechniquePtr gbuffer_technique_;
		RenderTechniquePtr gen_sm_technique_;
		RenderTechniquePtr emit_technique_;

		RenderEffectParameterPtr mvp_param_;
		RenderEffectParameterPtr model_view_param_;
		RenderEffectParameterPtr depth_near_far_invfar_param_;
	};

	class TorusObject : public SceneObjectHelper, public DeferredableObject
	{
	public:
		TorusObject(RenderablePtr const & mesh)
			: SceneObjectHelper(mesh, SOA_Cullable)
		{
		}

		void GenShadowMapPass(bool sm_pass)
		{
			checked_pointer_cast<RenderTorus>(renderable_)->GenShadowMapPass(sm_pass);
		}

		void EmitPass(bool emit_pass)
		{
			checked_pointer_cast<RenderTorus>(renderable_)->EmitPass(emit_pass);
		}

		bool IsEmit() const
		{
			return checked_pointer_cast<RenderTorus>(renderable_)->IsEmit();
		}
	};


	class RenderCone : public RenderableHelper
	{
	public:
		RenderCone(float cone_radius, float cone_height, float3 const & clr)
			: RenderableHelper(L"Cone"),
				gen_sm_pass_(false), emit_pass_(false)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			effect_ = rf.LoadEffect("GBuffer.fxml");

			gbuffer_technique_ = effect_->TechniqueByName("GBufferNoTexTech");
			gen_sm_technique_ = effect_->TechniqueByName("GenShadowMap");
			emit_technique_ = effect_->TechniqueByName("EmitOnly");
			technique_ = gbuffer_technique_;

			*(effect_->ParameterByName("diffuse_clr")) = float4(1, 1, 1, 1);
			*(effect_->ParameterByName("emit_clr")) = float4(clr.x(), clr.y(), clr.z(), 1);

			mvp_param_ = effect_->ParameterByName("mvp");
			model_view_param_ = effect_->ParameterByName("model_view");
			depth_near_far_invfar_param_ = effect_->ParameterByName("depth_near_far_invfar");

			std::vector<float3> pos;
			std::vector<uint16_t> index;
			CreateConeMesh(pos, index, 0, cone_radius, cone_height, 12);

			std::vector<float3> normal(pos.size());
			MathLib::compute_normal<float>(normal.begin(), index.begin(), index.end(), pos.begin(), pos.end());

			rl_ = rf.MakeRenderLayout();
			rl_->TopologyType(RenderLayout::TT_TriangleList);

			ElementInitData init_data;
			init_data.row_pitch = static_cast<uint32_t>(pos.size() * sizeof(pos[0]));
			init_data.slice_pitch = 0;
			init_data.data = &pos[0];

			rl_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data),
				boost::make_tuple(vertex_element(VEU_Position, 0, EF_BGR32F)));

			init_data.row_pitch = static_cast<uint32_t>(normal.size() * sizeof(normal[0]));
			init_data.slice_pitch = 0;
			init_data.data = &normal[0];
			rl_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data),
				boost::make_tuple(vertex_element(VEU_Normal, 0, EF_BGR32F)));
			rl_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data),
				boost::make_tuple(vertex_element(VEU_Tangent, 0, EF_BGR32F)));
			init_data.row_pitch = static_cast<uint32_t>(pos.size() * sizeof(float2));
			rl_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data),
				boost::make_tuple(vertex_element(VEU_TextureCoord, 0, EF_GR32F)));

			init_data.row_pitch = static_cast<uint32_t>(index.size() * sizeof(index[0]));
			init_data.slice_pitch = 0;
			init_data.data = &index[0];

			GraphicsBufferPtr ib = rf.MakeIndexBuffer(BU_Static, EAH_GPU_Read, &init_data);
			rl_->BindIndexStream(ib, EF_R16UI);

			box_ = MathLib::compute_bounding_box<float>(pos.begin(), pos.end());
		}

		void SetModelMatrix(float4x4 const & mat)
		{
			model_ = mat;
		}

		void GenShadowMapPass(bool sm_pass)
		{
			gen_sm_pass_ = sm_pass;
			if (gen_sm_pass_)
			{
				technique_ = gen_sm_technique_;
			}
			else
			{
				technique_ = gbuffer_technique_;
			}
		}

		void EmitPass(bool emit_pass)
		{
			emit_pass_ = emit_pass;
			if (emit_pass_)
			{
				technique_ = emit_technique_;
			}
			else
			{
				technique_ = gbuffer_technique_;
			}
		}

		void Update()
		{
			Camera const & camera = Context::Instance().AppInstance().ActiveCamera();

			float4x4 const & view = camera.ViewMatrix();
			float4x4 const & proj = camera.ProjMatrix();

			float4x4 mv = model_ * view;
			*mvp_param_ = mv * proj;
			*model_view_param_ = mv;

			*depth_near_far_invfar_param_ = float3(camera.NearPlane(), camera.FarPlane(), 1 / camera.FarPlane());
		}

	private:
		float4x4 model_;

		KlayGE::RenderEffectPtr effect_;
		bool gen_sm_pass_;
		bool emit_pass_;
		RenderTechniquePtr gbuffer_technique_;
		RenderTechniquePtr gen_sm_technique_;
		RenderTechniquePtr emit_technique_;

		RenderEffectParameterPtr mvp_param_;
		RenderEffectParameterPtr model_view_param_;
		RenderEffectParameterPtr depth_near_far_invfar_param_;
	};

	class ConeObject : public SceneObjectHelper, public DeferredableObject
	{
	public:
		ConeObject(float cone_radius, float cone_height, float org_angle, float rot_speed, float height, float3 const & clr)
			: SceneObjectHelper(SOA_Cullable), rot_speed_(rot_speed), height_(height)
		{
			renderable_ = MakeSharedPtr<RenderCone>(cone_radius, cone_height, clr);
			model_org_ = MathLib::rotation_x(org_angle);
		}

		void Update()
		{
			model_ = MathLib::scaling(0.1f, 0.1f, 0.1f) * model_org_ * MathLib::rotation_y(std::clock() * rot_speed_) * MathLib::translation(0.0f, height_, 0.0f);
			checked_pointer_cast<RenderCone>(renderable_)->SetModelMatrix(model_);
			checked_pointer_cast<RenderCone>(renderable_)->Update();
		}

		float4x4 const & GetModelMatrix() const
		{
			return model_;
		}

		void GenShadowMapPass(bool sm_pass)
		{
			checked_pointer_cast<RenderCone>(renderable_)->GenShadowMapPass(sm_pass);
		}

		void EmitPass(bool emit_pass)
		{
			checked_pointer_cast<RenderCone>(renderable_)->EmitPass(emit_pass);
		}

		bool IsEmit() const
		{
			return true;
		}

	private:
		float4x4 model_;
		float4x4 model_org_;
		float rot_speed_, height_;
	};

	class RenderSphere : public KMesh
	{
	public:
		RenderSphere(RenderModelPtr const & model, std::wstring const & name)
			: KMesh(model, name),
				gen_sm_pass_(false), emit_pass_(false)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			effect_ = rf.LoadEffect("GBuffer.fxml");

			gbuffer_technique_ = effect_->TechniqueByName("GBufferNoTexTech");
			gen_sm_technique_ = effect_->TechniqueByName("GenShadowMap");
			emit_technique_ = effect_->TechniqueByName("EmitOnly");
			technique_ = gbuffer_technique_;

			*(effect_->ParameterByName("diffuse_clr")) = float4(1, 1, 1, 1);

			mvp_param_ = effect_->ParameterByName("mvp");
			model_view_param_ = effect_->ParameterByName("model_view");
			depth_near_far_invfar_param_ = effect_->ParameterByName("depth_near_far_invfar");
		}

		void BuildMeshInfo()
		{
		}

		void SetModelMatrix(float4x4 const & mat)
		{
			model_ = mat;
		}

		void EmitClr(float3 const & clr)
		{
			*(effect_->ParameterByName("emit_clr")) = float4(clr.x(), clr.y(), clr.z(), 1);
		}

		void GenShadowMapPass(bool sm_pass)
		{
			gen_sm_pass_ = sm_pass;
			if (gen_sm_pass_)
			{
				technique_ = gen_sm_technique_;
			}
			else
			{
				technique_ = gbuffer_technique_;
			}
		}

		void EmitPass(bool emit_pass)
		{
			emit_pass_ = emit_pass;
			if (emit_pass_)
			{
				technique_ = emit_technique_;
			}
			else
			{
				technique_ = gbuffer_technique_;
			}
		}

		void Update()
		{
			Camera const & camera = Context::Instance().AppInstance().ActiveCamera();

			float4x4 const & view = camera.ViewMatrix();
			float4x4 const & proj = camera.ProjMatrix();

			float4x4 mv = model_ * view;
			*mvp_param_ = mv * proj;
			*model_view_param_ = mv;

			*depth_near_far_invfar_param_ = float3(camera.NearPlane(), camera.FarPlane(), 1 / camera.FarPlane());
		}

	private:
		float4x4 model_;

		KlayGE::RenderEffectPtr effect_;
		bool gen_sm_pass_;
		bool emit_pass_;
		RenderTechniquePtr gbuffer_technique_;
		RenderTechniquePtr gen_sm_technique_;
		RenderTechniquePtr emit_technique_;

		RenderEffectParameterPtr mvp_param_;
		RenderEffectParameterPtr model_view_param_;
		RenderEffectParameterPtr depth_near_far_invfar_param_;
	};

	class SphereObject : public SceneObjectHelper, public DeferredableObject
	{
	public:
		SphereObject(std::string const & model_name, float move_speed, float3 const & pos, float3 const & clr)
			: SceneObjectHelper(SOA_Cullable), move_speed_(move_speed), pos_(pos)
		{
			renderable_ = LoadModel(model_name, EAH_GPU_Read, CreateKModelFactory<RenderModel>(), CreateKMeshFactory<RenderSphere>())()->Mesh(0);
			checked_pointer_cast<RenderSphere>(renderable_)->EmitClr(clr);
		}

		void Update()
		{
			model_ = MathLib::scaling(0.1f, 0.1f, 0.1f) * MathLib::translation(sin(std::clock() * move_speed_), 0.0f, 0.0f) * MathLib::translation(pos_);
			checked_pointer_cast<RenderSphere>(renderable_)->SetModelMatrix(model_);
			checked_pointer_cast<RenderSphere>(renderable_)->Update();
		}

		float4x4 const & GetModelMatrix() const
		{
			return model_;
		}

		void GenShadowMapPass(bool sm_pass)
		{
			checked_pointer_cast<RenderSphere>(renderable_)->GenShadowMapPass(sm_pass);
		}

		void EmitPass(bool emit_pass)
		{
			checked_pointer_cast<RenderSphere>(renderable_)->EmitPass(emit_pass);
		}

		bool IsEmit() const
		{
			return true;
		}

	private:
		float4x4 model_;
		float move_speed_;
		float3 pos_;
	};

	class RenderableDeferredHDRSkyBox : public RenderableHDRSkyBox
	{
	public:
		RenderableDeferredHDRSkyBox()
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			technique_ = rf.LoadEffect("GBuffer.fxml")->TechniqueByName("GBufferSkyBoxTech");

			skybox_cube_tex_ep_ = technique_->Effect().ParameterByName("skybox_tex");
			skybox_Ccube_tex_ep_ = technique_->Effect().ParameterByName("skybox_C_tex");
			inv_mvp_ep_ = technique_->Effect().ParameterByName("inv_mvp");
		}
	};

	class SceneObjectDeferredHDRSkyBox : public SceneObjectHDRSkyBox
	{
	public:
		SceneObjectDeferredHDRSkyBox()
		{
			renderable_ = MakeSharedPtr<RenderableDeferredHDRSkyBox>();
		}
	};

	class AdaptiveAntiAliasPostProcess : public PostProcess
	{
	public:
		AdaptiveAntiAliasPostProcess()
		{
			input_pins_.push_back(std::make_pair("src_tex", TexturePtr()));
			input_pins_.push_back(std::make_pair("color_tex", TexturePtr()));

			this->Technique(Context::Instance().RenderFactoryInstance().LoadEffect("AdaptiveAntiAliasPP.fxml")->TechniqueByName("AdaptiveAntiAlias"));
		}

		void InputPin(uint32_t index, TexturePtr const & tex, bool flipping)
		{
			PostProcess::InputPin(index, tex, flipping);
			if ((0 == index) && tex)
			{
				*(technique_->Effect().ParameterByName("inv_width_height")) = float2(1.0f / tex->Width(0), 1.0f / tex->Height(0));
			}
		}

		void ShowEdge(bool se)
		{
			if (se)
			{
				technique_ = technique_->Effect().TechniqueByName("AdaptiveAntiAliasShowEdge");
			}
			else
			{
				technique_ = technique_->Effect().TechniqueByName("AdaptiveAntiAlias");
			}
		}
	};

	class SSAOPostProcess : public PostProcess
	{
	public:
		SSAOPostProcess()
			: PostProcess(std::vector<std::string>(1, "src_tex"), Context::Instance().RenderFactoryInstance().LoadEffect("SSAOPP.fxml")->TechniqueByName("HDAO")),
				hd_mode_(true)
		{
			blur_pp_ = MakeSharedPtr<BlurPostProcess<SeparableGaussianFilterPostProcess> >(8, 1.0f);

			depth_near_far_invfar_param_ = technique_->Effect().ParameterByName("depth_near_far_invfar");
			proj_param_ = technique_->Effect().ParameterByName("proj");
			rt_size_inv_size_param_ = technique_->Effect().ParameterByName("rt_size_inv_size");
		}

		void Destinate(FrameBufferPtr const & fb)
		{
			PostProcess::Destinate(fb);

			if (!ssao_buffer_)
			{
				RenderFactory& rf = Context::Instance().RenderFactoryInstance();
				RenderEngine& re = rf.RenderEngineInstance();

				ssao_buffer_ = rf.MakeFrameBuffer();
				ssao_buffer_->GetViewport().camera = re.CurFrameBuffer()->GetViewport().camera;

				try
				{
					ssao_tex_ = rf.MakeTexture2D(fb->Width(), fb->Height(), 1, 1, EF_R16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
				}
				catch (...)
				{
					ssao_tex_ = rf.MakeTexture2D(fb->Width(), fb->Height(), 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
				}
				ssao_buffer_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*ssao_tex_, 0, 0));

				blur_pp_->InputPin(0, ssao_tex_, ssao_buffer_->RequiresFlipping());
				blur_pp_->Destinate(fb);
			}
		}

		void HDAOMode(bool hd)
		{
			hd_mode_ = hd;
			if (hd_mode_)
			{
				this->Technique(technique_->Effect().TechniqueByName("HDAO"));
				PostProcess::Destinate(blur_pp_->Destinate());
			}
			else
			{
				this->Technique(technique_->Effect().TechniqueByName("SSAO"));
				PostProcess::Destinate(ssao_buffer_);
			}
		}

		void Apply()
		{
			PostProcess::Apply();

			if (!hd_mode_)
			{
				blur_pp_->Apply();
			}
		}

		void OnRenderBegin()
		{
			PostProcess::OnRenderBegin();

			Camera const & camera = Context::Instance().AppInstance().ActiveCamera();
			*depth_near_far_invfar_param_ = float3(camera.NearPlane(), camera.FarPlane(), 1 / camera.FarPlane());

			float4x4 const & proj = camera.ProjMatrix();
			*proj_param_ = float2(proj(0, 0), proj(1, 1));

			RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
			*rt_size_inv_size_param_ = float4(1.0f * re.CurFrameBuffer()->Width(), 1.0f * re.CurFrameBuffer()->Height(),
				1.0f / re.CurFrameBuffer()->Width(), 1.0f / re.CurFrameBuffer()->Height());
		}

	private:
		bool hd_mode_;

		PostProcessPtr blur_pp_;

		FrameBufferPtr ssao_buffer_;
		TexturePtr ssao_tex_;

		RenderEffectParameterPtr depth_near_far_invfar_param_;
		RenderEffectParameterPtr proj_param_;
		RenderEffectParameterPtr rt_size_inv_size_param_;
	};


	enum
	{
		Exit,
	};

	InputActionDefine actions[] =
	{
		InputActionDefine(Exit, KS_Escape),
	};

	bool ConfirmDevice()
	{
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		RenderEngine& re = rf.RenderEngineInstance();
		RenderDeviceCaps const & caps = re.DeviceCaps();
		if (caps.max_shader_model < 2)
		{
			return false;
		}
		if (caps.max_simultaneous_rts < 2)
		{
			return false;
		}

		try
		{
			TexturePtr temp_tex = rf.MakeTexture2D(800, 600, 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
			rf.Make2DRenderView(*temp_tex, 0, 0);
			rf.MakeDepthStencilRenderView(800, 600, EF_D16, 1, 0);
		}
		catch (...)
		{
			return false;
		}

		return true;
	}
}

int main()
{
	ResLoader::Instance().AddPath("../Samples/media/Common");
	ResLoader::Instance().AddPath("../Samples/media/DeferredShading");

	RenderSettings settings = Context::Instance().LoadCfg("KlayGE.cfg");
	settings.ConfirmDevice = ConfirmDevice;

	DeferredShadingApp app("DeferredShading", settings);
	app.Create();
	app.Run();

	return 0;
}

DeferredShadingApp::DeferredShadingApp(std::string const & name, RenderSettings const & settings)
			: App3DFramework(name, settings),
				anti_alias_enabled_(true), ssao_type_(0)
{
}

void DeferredShadingApp::InitObjects()
{
	boost::function<RenderModelPtr()> model_ml = LoadModel("sponza.meshml", EAH_GPU_Read, CreateKModelFactory<RenderModel>(), CreateKMeshFactory<RenderTorus>());

	font_ = Context::Instance().RenderFactoryInstance().MakeFont("gkai00mp.kfont");

	deferred_shading_ = MakeSharedPtr<DeferredShadingLayer>();
	ambient_light_id_ = deferred_shading_->AddAmbientLight(float3(1, 1, 1));
	point_light_id_ = deferred_shading_->AddPointLight(0, float3(0, 0, 0), float3(1, 1, 1), float3(0, 0.5f, 0));
	spot_light_id_[0] = deferred_shading_->AddSpotLight(0, float3(0, 0, 0), float3(0, 0, 0), PI / 6, PI / 8, float3(1, 0, 0), float3(0, 0.5f, 0));
	spot_light_id_[1] = deferred_shading_->AddSpotLight(0, float3(0, 0, 0), float3(0, 0, 0), PI / 4, PI / 6, float3(0, 1, 0), float3(0, 0.5f, 0));

	point_light_src_ = MakeSharedPtr<SphereObject>("sphere.meshml", 1 / 1000.0f, float3(2, 5, 0), deferred_shading_->LightColor(point_light_id_));
	spot_light_src_[0] = MakeSharedPtr<ConeObject>(sqrt(3.0f) / 3, 1.0f, PI, 1 / 1400.0f, 2.0f, deferred_shading_->LightColor(spot_light_id_[0]));
	spot_light_src_[1] = MakeSharedPtr<ConeObject>(1.0f, 1.0f, 0.0f, -1 / 700.0f, 1.7f, deferred_shading_->LightColor(spot_light_id_[1]));
	point_light_src_->AddToSceneManager();
	spot_light_src_[0]->AddToSceneManager();
	spot_light_src_[1]->AddToSceneManager();

	this->LookAt(float3(-2, 2, 0), float3(0, 2, 0));
	this->Proj(0.1f, 500.0f);

	RenderFactory& rf = Context::Instance().RenderFactoryInstance();
	RenderEngine& re = rf.RenderEngineInstance();

	TexturePtr y_cube_map = LoadTexture("Lake_CraterLake03_y.dds", EAH_GPU_Read)();
	TexturePtr c_cube_map = LoadTexture("Lake_CraterLake03_c.dds", EAH_GPU_Read)();
	sky_box_ = MakeSharedPtr<SceneObjectDeferredHDRSkyBox>();
	checked_pointer_cast<SceneObjectDeferredHDRSkyBox>(sky_box_)->CompressedCubeMap(y_cube_map, c_cube_map);
	sky_box_->AddToSceneManager();

	ssao_buffer_ = rf.MakeFrameBuffer();
	ssao_buffer_->GetViewport().camera = re.CurFrameBuffer()->GetViewport().camera;

	hdr_buffer_ = rf.MakeFrameBuffer();
	hdr_buffer_->GetViewport().camera = re.CurFrameBuffer()->GetViewport().camera;

	fpcController_.Scalers(0.05f, 0.5f);

	InputEngine& inputEngine(Context::Instance().InputFactoryInstance().InputEngineInstance());
	InputActionMap actionMap;
	actionMap.AddActions(actions, actions + sizeof(actions) / sizeof(actions[0]));

	action_handler_t input_handler = MakeSharedPtr<input_signal>();
	input_handler->connect(boost::bind(&DeferredShadingApp::InputHandler, this, _1, _2));
	inputEngine.ActionMap(actionMap, input_handler, true);

	edge_anti_alias_ = MakeSharedPtr<AdaptiveAntiAliasPostProcess>();
	ssao_pp_ = MakeSharedPtr<SSAOPostProcess>();
	hdr_pp_ = MakeSharedPtr<HDRPostProcess>(true, false);

	UIManager::Instance().Load(ResLoader::Instance().Load("DeferredShading.uiml"));
	dialog_ = UIManager::Instance().GetDialogs()[0];

	id_buffer_combo_ = dialog_->IDFromName("BufferCombo");
	id_anti_alias_ = dialog_->IDFromName("AntiAlias");
	id_ssao_combo_ = dialog_->IDFromName("SSAOCombo");
	id_ctrl_camera_ = dialog_->IDFromName("CtrlCamera");

	dialog_->Control<UIComboBox>(id_buffer_combo_)->OnSelectionChangedEvent().connect(boost::bind(&DeferredShadingApp::BufferChangedHandler, this, _1));
	this->BufferChangedHandler(*dialog_->Control<UIComboBox>(id_buffer_combo_));

	dialog_->Control<UICheckBox>(id_anti_alias_)->OnChangedEvent().connect(boost::bind(&DeferredShadingApp::AntiAliasHandler, this, _1));
	this->AntiAliasHandler(*dialog_->Control<UICheckBox>(id_anti_alias_));
	dialog_->Control<UIComboBox>(id_ssao_combo_)->OnSelectionChangedEvent().connect(boost::bind(&DeferredShadingApp::SSAOChangedHandler, this, _1));
	this->SSAOChangedHandler(*dialog_->Control<UIComboBox>(id_ssao_combo_));
	dialog_->Control<UICheckBox>(id_ctrl_camera_)->OnChangedEvent().connect(boost::bind(&DeferredShadingApp::CtrlCameraHandler, this, _1));
	this->CtrlCameraHandler(*dialog_->Control<UICheckBox>(id_ctrl_camera_));

	RenderModelPtr model = model_ml();
	scene_objs_.resize(model->NumMeshes());
	for (size_t i = 0; i < model->NumMeshes(); ++ i)
	{
		scene_objs_[i] = MakeSharedPtr<TorusObject>(model->Mesh(i));
		scene_objs_[i]->AddToSceneManager();
	}
}

void DeferredShadingApp::OnResize(uint32_t width, uint32_t height)
{
	App3DFramework::OnResize(width, height);
	deferred_shading_->OnResize(width, height);

	RenderFactory& rf = Context::Instance().RenderFactoryInstance();

	try
	{
		ssao_tex_ = rf.MakeTexture2D(width, height, 1, 1, EF_R16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	}
	catch (...)
	{
		ssao_tex_ = rf.MakeTexture2D(width, height, 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	}
	ssao_buffer_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*ssao_tex_, 0, 0));

	hdr_tex_ = rf.MakeTexture2D(width, height, 1, 1, deferred_shading_->ShadedTex()->Format(), 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	hdr_buffer_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*hdr_tex_, 0, 0));

	deferred_shading_->SSAOTex(ssao_tex_);

	edge_anti_alias_->InputPin(0, deferred_shading_->NormalDepthTex(), deferred_shading_->ShadedFB()->RequiresFlipping());
	edge_anti_alias_->InputPin(1, deferred_shading_->ShadedTex(), deferred_shading_->ShadedFB()->RequiresFlipping());
	edge_anti_alias_->Destinate(hdr_buffer_);
	//edge_anti_alias_->Destinate(FrameBufferPtr());

	hdr_pp_->InputPin(0, hdr_tex_, hdr_buffer_->RequiresFlipping());
	hdr_pp_->Destinate(FrameBufferPtr());

	ssao_pp_->InputPin(0, deferred_shading_->NormalDepthTex(), deferred_shading_->GBufferFB()->RequiresFlipping());
	ssao_pp_->Destinate(ssao_buffer_);

	UIManager::Instance().SettleCtrls(width, height);
}

void DeferredShadingApp::InputHandler(InputEngine const & /*sender*/, InputAction const & action)
{
	switch (action.first)
	{
	case Exit:
		this->Quit();
		break;
	}
}

void DeferredShadingApp::BufferChangedHandler(UIComboBox const & sender)
{
	buffer_type_ = sender.GetSelectedIndex();
	deferred_shading_->BufferType(buffer_type_);

	if (buffer_type_ != 0)
	{
		anti_alias_enabled_ = false;
	}
	else
	{
		anti_alias_enabled_ = true;
		edge_anti_alias_->Destinate(hdr_buffer_);
	}
	dialog_->Control<UICheckBox>(id_anti_alias_)->SetChecked(anti_alias_enabled_);

	checked_pointer_cast<AdaptiveAntiAliasPostProcess>(edge_anti_alias_)->ShowEdge(6 == buffer_type_);
	if (6 == buffer_type_)
	{
		edge_anti_alias_->Destinate(FrameBufferPtr());
	}
	else
	{
		edge_anti_alias_->Destinate(hdr_buffer_);
	}
}

void DeferredShadingApp::AntiAliasHandler(UICheckBox const & sender)
{
	if (0 == buffer_type_)
	{
		anti_alias_enabled_ = sender.GetChecked();
		if (anti_alias_enabled_)
		{
			edge_anti_alias_->Destinate(hdr_buffer_);
			//edge_anti_alias_->Destinate(FrameBufferPtr());

			if (hdr_tex_)
			{
				hdr_pp_->InputPin(0, hdr_tex_, hdr_buffer_->RequiresFlipping());
			}
		}
		else
		{
			hdr_pp_->InputPin(0, deferred_shading_->ShadedTex(), deferred_shading_->ShadedFB()->RequiresFlipping());
		}
	}
}

void DeferredShadingApp::SSAOChangedHandler(UIComboBox const & sender)
{
	if ((0 == buffer_type_) || (7 == buffer_type_))
	{
		ssao_type_ = sender.GetSelectedIndex();
		if (ssao_type_ != 2)
		{
			checked_pointer_cast<SSAOPostProcess>(ssao_pp_)->HDAOMode(0 == ssao_type_);
		}
		deferred_shading_->SSAOEnabled(ssao_type_ != 2);
	}
}

void DeferredShadingApp::CtrlCameraHandler(UICheckBox const & sender)
{
	if (sender.GetChecked())
	{
		fpcController_.AttachCamera(this->ActiveCamera());
	}
	else
	{
		fpcController_.DetachCamera();
	}
}

void DeferredShadingApp::DoUpdateOverlay()
{
	RenderEngine& renderEngine(Context::Instance().RenderFactoryInstance().RenderEngineInstance());

	UIManager::Instance().Render();

	FrameBuffer& rw = *checked_pointer_cast<FrameBuffer>(renderEngine.CurFrameBuffer());

	font_->RenderText(0, 0, Color(1, 1, 0, 1), L"Deferred Shading", 16);
	font_->RenderText(0, 18, Color(1, 1, 0, 1), rw.Description(), 16);

	std::wostringstream stream;
	stream.precision(2);
	stream << fixed << this->FPS() << " FPS";
	font_->RenderText(0, 36, Color(1, 1, 0, 1), stream.str(), 16);

	stream.str(L"");
	stream << num_objs_rendered_ << " Scene objects "
		<< num_renderable_rendered_ << " Renderables "
		<< num_primitives_rendered_ << " Primitives "
		<< num_vertices_rendered_ << " Vertices";
	font_->RenderText(0, 54, Color(1, 1, 1, 1), stream.str(), 16);
}

uint32_t DeferredShadingApp::DoUpdate(uint32_t pass)
{
	SceneManager& sceneMgr(Context::Instance().SceneManagerInstance());
	RenderEngine& renderEngine(Context::Instance().RenderFactoryInstance().RenderEngineInstance());

	switch (pass)
	{
	case 0:
		{
			float4x4 model_mat = checked_pointer_cast<SphereObject>(point_light_src_)->GetModelMatrix();
			float3 p = MathLib::transform_coord(float3(0, 0, 0), model_mat);
			deferred_shading_->LightPos(point_light_id_, p);
		}
		for (int i = 0; i < 2; ++ i)
		{
			float4x4 model_mat = checked_pointer_cast<ConeObject>(spot_light_src_[i])->GetModelMatrix();
			float3 p = MathLib::transform_coord(float3(0, 0, 0), model_mat);
			float3 d = MathLib::normalize(MathLib::transform_normal(float3(0, 0, 1), model_mat));
			deferred_shading_->LightPos(spot_light_id_[i], p);
			deferred_shading_->LightDir(spot_light_id_[i], d);
		}

		point_light_src_->Visible(true);
		spot_light_src_[0]->Visible(true);
		spot_light_src_[1]->Visible(true);

		break;

	case 1:
		num_objs_rendered_ = sceneMgr.NumObjectsRendered();
		num_renderable_rendered_ = sceneMgr.NumRenderablesRendered();
		num_primitives_rendered_ = sceneMgr.NumPrimitivesRendered();
		num_vertices_rendered_ = sceneMgr.NumVerticesRendered();

		point_light_src_->Visible(false);
		spot_light_src_[0]->Visible(false);
		spot_light_src_[1]->Visible(false);

		if (((0 == buffer_type_) && (ssao_type_ != 2)) || (7 == buffer_type_))
		{
			ssao_pp_->Apply();
		}

		break;
	}

	uint32_t ret = deferred_shading_->Update(pass);
	if (App3DFramework::URV_Finished == ret)
	{
		renderEngine.BindFrameBuffer(FrameBufferPtr());
		renderEngine.CurFrameBuffer()->Attached(FrameBuffer::ATT_DepthStencil)->Clear(1.0f);
		if (((0 == buffer_type_) && anti_alias_enabled_) || (6 == buffer_type_))
		{
			edge_anti_alias_->Apply();
		}
		if (0 == buffer_type_)
		{
			hdr_pp_->Apply();
		}
	}

	return ret;
}
