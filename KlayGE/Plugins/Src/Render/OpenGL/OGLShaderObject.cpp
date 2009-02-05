// OGLShaderObject.cpp
// KlayGE OpenGL shader对象类 实现文件
// Ver 3.7.0
// 版权所有(C) 龚敏敏, 2006-2008
// Homepage: http://klayge.sourceforge.net
//
// 3.7.0
// 改为直接传入RenderEffect (2008.7.4)
//
// 3.5.0
// 初次建立 (2006.11.2)
//
// 修改记录
//////////////////////////////////////////////////////////////////////////////////

#include <KlayGE/KlayGE.hpp>
#include <KlayGE/ThrowErr.hpp>
#include <KlayGE/Util.hpp>
#include <KlayGE/COMPtr.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/Math.hpp>
#include <KlayGE/Matrix.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>

#include <string>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <boost/assert.hpp>
#include <boost/lexical_cast.hpp>
#ifdef KLAYGE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable: 4512)
#endif
#include <boost/assign.hpp>
#ifdef KLAYGE_COMPILER_MSVC
#pragma warning(pop)
#endif
#include <boost/assign/std/vector.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>

#include <glloader/glloader.h>

#include <KlayGE/OpenGL/OGLRenderFactory.hpp>
#include <KlayGE/OpenGL/OGLRenderFactoryInternal.hpp>
#include <KlayGE/OpenGL/OGLRenderEngine.hpp>
#include <KlayGE/OpenGL/OGLMapping.hpp>
#include <KlayGE/OpenGL/OGLTexture.hpp>
#include <KlayGE/OpenGL/OGLRenderStateObject.hpp>
#include <KlayGE/OpenGL/OGLShaderObject.hpp>

namespace
{
	using namespace KlayGE;


	char const * predefined_funcs = "\n								\
	float4 tex1DLevel(sampler s, float location, float lod)\n		\
	{\n																\
		return tex1Dlod(s, float4(location, 0, 0, lod));\n			\
	}\n																\
	\n																\
	float4 tex2DLevel(sampler s, float2 location, float lod)\n		\
	{\n																\
		return tex2Dlod(s, float4(location, 0, lod));\n				\
	}\n																\
	\n																\
	float4 tex3DLevel(sampler s, float3 location, float lod)\n		\
	{\n																\
		return tex3Dlod(s, float4(location, lod));\n				\
	}\n																\
	\n																\
	float4 texCUBELevel(sampler s, float3 location, float lod)\n	\
	{\n																\
		return texCUBElod(s, float4(location, lod));\n				\
	}\n																\
	";

	template <typename SrcType>
	class SetOGLShaderParameter
	{
	};

	template <>
	class SetOGLShaderParameter<bool>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			bool v;
			param_->Value(v);

			cgSetParameter1i(cg_param_, v);
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<int>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			int v;
			param_->Value(v);

			cgSetParameter1i(cg_param_, v);
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			float v;
			param_->Value(v);

			cgSetParameter1f(cg_param_, v);
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float2>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			float2 v;
			param_->Value(v);

			cgSetParameter2fv(cg_param_, &v.x());
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float3>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			float3 v;
			param_->Value(v);

			cgSetParameter3fv(cg_param_, &v.x());
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float4>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			float4 v;
			param_->Value(v);

			cgSetParameter4fv(cg_param_, &v.x());
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float4x4>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			float4x4 v;
			param_->Value(v);

			cgGLSetMatrixParameterfr(cg_param_, &v[0]);
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<bool*>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			std::vector<bool> v;
			param_->Value(v);

			if (!v.empty())
			{
				std::vector<int> tmp(v.begin(), v.end());
				cgSetParameterValueir(cg_param_, static_cast<int>(tmp.size()), &tmp[0]);
			}
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<int*>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			std::vector<int> v;
			param_->Value(v);

			if (!v.empty())
			{
				cgSetParameterValueir(cg_param_, static_cast<int>(v.size()), &v[0]);
			}
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float*>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			std::vector<float> v;
			param_->Value(v);

			if (!v.empty())
			{
				cgGLSetParameterArray1f(cg_param_, 0, static_cast<int>(v.size()), &v[0]);
			}
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float4*>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			std::vector<float4> v;
			param_->Value(v);

			if (!v.empty())
			{
				cgGLSetParameterArray4f(cg_param_, 0, static_cast<long>(v.size()), &v[0][0]);
			}
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<float4x4*>
	{
	public:
		SetOGLShaderParameter(CGparameter cg_param, RenderEffectParameterPtr const & param)
			: cg_param_(cg_param), param_(param)
		{
		}

		void operator()()
		{
			std::vector<float4x4> v;
			param_->Value(v);

			if (!v.empty())
			{
				cgGLSetMatrixParameterArrayfr(cg_param_, 0, static_cast<long>(v.size()), &v[0][0]);
			}
		}

	private:
		CGparameter cg_param_;
		RenderEffectParameterPtr param_;
	};

	template <>
	class SetOGLShaderParameter<std::pair<TexturePtr, SamplerStateObjectPtr> >
	{
	public:
		SetOGLShaderParameter(std::pair<TexturePtr, SamplerStateObjectPtr>& sampler,
					CGparameter cg_param, GLuint stage,
					RenderEffectParameterPtr const & tex_param, RenderEffectParameterPtr const & sampler_param)
			: sampler_(&sampler), cg_param_(cg_param), stage_(stage), tex_param_(tex_param), sampler_param_(sampler_param)
		{
		}

		void operator()()
		{
			tex_param_->Value(sampler_->first);
			sampler_param_->Value(sampler_->second);

			if (sampler_ && sampler_->first)
			{
				GLuint const gl_tex = checked_pointer_cast<OGLTexture>(sampler_->first)->GLTexture();
				checked_pointer_cast<OGLSamplerStateObject>(sampler_->second)->Active(stage_, sampler_->first);

				cgGLSetTextureParameter(cg_param_, gl_tex);
			}
		}

	private:
		std::pair<TexturePtr, SamplerStateObjectPtr>* sampler_;
		CGparameter cg_param_;
		GLuint stage_;
		RenderEffectParameterPtr tex_param_;
		RenderEffectParameterPtr sampler_param_;
	};
}

namespace KlayGE
{
	OGLShaderObject::OGLShaderObject()
	{
		is_shader_validate_.assign(true);
	}

	OGLShaderObject::~OGLShaderObject()
	{
		for (int i = 0; i < ST_NumShaderTypes; ++ i)
		{
			BOOST_FOREACH(BOOST_TYPEOF(param_binds_[i])::reference pb, param_binds_[i])
			{
				cgDestroyParameter(pb.cg_param);
			}

			cgDestroyProgram(shaders_[i]);
		}
	}

	std::string OGLShaderObject::GenShaderText(RenderEffect const & effect) const
	{
		std::stringstream shader_ss;
		bool sample_helper = false;
		for (uint32_t i = 0; i < effect.NumShaders(); ++ i)
		{
			std::string const & s = effect.ShaderByIndex(i).str();
			boost::char_separator<char> sep("", " \t\n.,():;+-*/%&!|^[]{}'\"?");
			boost::tokenizer<boost::char_separator<char> > tok(s, sep);
			std::string this_token;
			for (BOOST_AUTO(beg, tok.begin()); beg != tok.end();)
			{
				this_token = *beg;

				RenderEffectParameterPtr const & param = effect.ParameterByName(this_token);
				if (param &&
					((REDT_texture1D == param->type()) || (REDT_texture2D == param->type()) || (REDT_texture3D == param->type())
						|| (REDT_textureCUBE == param->type())))
				{
					std::vector<std::string> sample_tokens;
					sample_tokens.push_back(this_token);
					++ beg;
					if ("." == *beg)
					{
						while (*beg != ",")
						{
							if ((*beg != " ") && (*beg != "\t") && (*beg != "\n"))
							{
								sample_tokens.push_back(*beg);
							}
							++ beg;
						}

						std::string combined_sampler_name = sample_tokens[0] + "__" + sample_tokens[4];
						bool found = false;
						for (uint32_t i = 0; i < tex_sampler_binds_.size(); ++ i)
						{
							if (tex_sampler_binds_[i].first == combined_sampler_name)
							{
								found = true;
								break;
							}
						}
						if (!found)
						{
							tex_sampler_binds_.push_back(std::make_pair(combined_sampler_name,
								std::make_pair(param, effect.ParameterByName(sample_tokens[4]))));
						}

						if ((!sample_helper) && ("SampleLevel" == sample_tokens[2]))
						{
							sample_helper = true;
						}

						switch (param->type())
						{
						case REDT_texture1D:
							if (("Sample" == sample_tokens[2]) || ("SampleGrad" == sample_tokens[2]))
							{
								shader_ss << "tex1D";
							}
							else
							{
								if ("SampleLevel" == sample_tokens[2])
								{
									shader_ss << "tex1DLevel";
								}
							}
							break;

						case REDT_texture2D:
							if (("Sample" == sample_tokens[2]) || ("SampleGrad" == sample_tokens[2]))
							{
								shader_ss << "tex2D";
							}
							else
							{
								if ("SampleLevel" == sample_tokens[2])
								{
									shader_ss << "tex2DLevel";
								}
							}
							break;

						case REDT_texture3D:
							if (("Sample" == sample_tokens[2]) || ("SampleGrad" == sample_tokens[2]))
							{
								shader_ss << "tex3D";
							}
							else
							{
								if ("SampleLevel" == sample_tokens[2])
								{
									shader_ss << "tex3DLevel";
								}
							}
							break;

						case REDT_textureCUBE:
							if (("Sample" == sample_tokens[2]) || ("SampleGrad" == sample_tokens[2]))
							{
								shader_ss << "texCUBE";
							}
							else
							{
								if ("SampleLevel" == sample_tokens[2])
								{
									shader_ss << "texCUBELevel";
								}
							}
							break;
						}
						shader_ss << "(" << combined_sampler_name << ",";

						++ beg;
					}
					else
					{
						shader_ss << this_token;
					}
				}
				else
				{
					if ("SV_Position" == this_token)
					{
						shader_ss << "POSITION";
					}
					else
					{
						if ("SV_Depth" == this_token)
						{
							shader_ss << "DEPTH";
						}
						else
						{
							if (0 == this_token.find("SV_Target"))
							{
								shader_ss << "COLOR" << this_token.substr(9);
							}
							else
							{
								shader_ss << this_token;
							}
						}
					}

					++ beg;
				}
			}
			shader_ss << std::endl;
		}

		std::stringstream ss;
		if (sample_helper)
		{
			ss << predefined_funcs << std::endl;
		}

		BOOST_AUTO(cbuffers, effect.CBuffers());
		BOOST_FOREACH(BOOST_TYPEOF(cbuffers)::const_reference cbuff, cbuffers)
		{
			BOOST_FOREACH(BOOST_TYPEOF(cbuff.second)::const_reference param_index, cbuff.second)
			{
				RenderEffectParameter& param = *effect.ParameterByIndex(param_index);

				ss << effect.TypeName(param.type()) << " " << *param.Name();
				if (param.ArraySize() != 0)
				{
					ss << "[" << param.ArraySize() << "]";
				}
				ss << ";" << std::endl;
			}
		}

		for (uint32_t i = 0; i < tex_sampler_binds_.size(); ++ i)
		{
			ss << "sampler " << tex_sampler_binds_[i].first << ": TEXUNIT" << i << ";" << std::endl;
		}

		ss << shader_ss.str();

		return ss.str();
	}

	void OGLShaderObject::SetShader(RenderEffect& effect, ShaderType type, boost::shared_ptr<std::vector<shader_desc> > const & shader_descs,
			boost::shared_ptr<std::string> const & shader_text)
	{
		is_shader_validate_[type] = true;

		shader_descs_ = shader_descs;
		shader_text_ = shader_text;
		*shader_text_ = "#define KLAYGE_OPENGL 1\n\n" + *shader_text_;
		if (!Context::Instance().RenderFactoryInstance().RenderEngineInstance().DeviceCaps().bc5_support)
		{
			*shader_text_ = "#define KLAYGE_BC5_AS_AG\n" + *shader_text_;
		}
		else
		{
			*shader_text_ = "#define KLAYGE_BC5_AS_GA\n" + *shader_text_;
		}

		std::string profile = (*shader_descs_)[type].profile;
		switch (type)
		{
		case ST_VertexShader:
			if ("auto" == profile)
			{
				profiles_[type] = cgGLGetLatestProfile(CG_GL_VERTEX);
			}
			else
			{
				profiles_[type] = cgGetProfile(profile.c_str());
			}
			break;

		case ST_PixelShader:
			if ("auto" == profile)
			{
				profiles_[type] = cgGLGetLatestProfile(CG_GL_FRAGMENT);
			}
			else
			{
				profiles_[type] = cgGetProfile(profile.c_str());
			}
			break;

		case ST_GeometryShader:
			if ("auto" == profile)
			{
				profiles_[type] = cgGLGetLatestProfile(CG_GL_GEOMETRY);
			}
			else
			{
				profiles_[type] = cgGetProfile(profile.c_str());
			}
			break;

		default:
			BOOST_ASSERT(false);
			break;
		}

		OGLRenderFactory& render_factory(*checked_cast<OGLRenderFactory*>(&Context::Instance().RenderFactoryInstance()));

		shaders_[type] = cgCreateProgram(render_factory.CGContext(),
				CG_SOURCE, shader_text->c_str(), profiles_[type], (*shader_descs_)[type].func_name.c_str(), NULL);

		CGerror error;
		char const * err_string = cgGetLastErrorString(&error);
		if (error != CG_NO_ERROR)
		{
#ifdef KLAYGE_DEBUG
			std::istringstream iss(*shader_text);
			std::string s;
			int line = 1;
			while (iss)
			{
				std::getline(iss, s);
				std::cerr << line << " " << s << std::endl;
				++ line;
			}
			std::cerr << err_string << std::endl;
			if (CG_COMPILER_ERROR == error)
			{
				std::cerr << cgGetLastListing(render_factory.CGContext()) << std::endl;
			}
#else
			UNREF_PARAM(err_string);
#endif

			is_shader_validate_[type] = false;
		}

		cgGLLoadProgram(shaders_[type]);

		RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		switch (type)
		{
		case ST_VertexShader:
			samplers_[type].resize(re.DeviceCaps().max_vertex_texture_units);
			break;

		case ST_PixelShader:
			samplers_[type].resize(re.DeviceCaps().max_texture_units);
			break;

		case ST_GeometryShader:
			samplers_[type].resize(re.DeviceCaps().max_texture_units);
			break;

		default:
			BOOST_ASSERT(false);
			break;
		}

		CGparameter cg_param = cgGetFirstParameter(shaders_[type], CG_GLOBAL);
		while (cg_param)
		{
			if (cgIsParameterUsed(cg_param, shaders_[type])
				&& (CG_PARAMETERCLASS_OBJECT != cgGetParameterClass(cg_param)))
			{
				char const * pname = cgGetParameterName(cg_param);

				RenderEffectParameterPtr const & p = effect.ParameterByName(pname);
				if (p)
				{
					param_binds_[type].push_back(this->GetBindFunc(cg_param, p));
				}
				else
				{
					for (size_t i = 0; i < tex_sampler_binds_.size(); ++ i)
					{
						if (tex_sampler_binds_[i].first == pname)
						{
							parameter_bind_t pb;
							pb.combined_sampler_name = tex_sampler_binds_[i].first;
							pb.cg_param = cg_param;

							uint32_t index = cgGLGetTextureEnum(cg_param) - GL_TEXTURE0;
							BOOST_ASSERT(index < samplers_[type].size());

							pb.func = SetOGLShaderParameter<std::pair<TexturePtr, SamplerStateObjectPtr> >(samplers_[type][index], cg_param,
								index, tex_sampler_binds_[i].second.first, tex_sampler_binds_[i].second.second);
							
							param_binds_[type].push_back(pb);
							break;
						}
					}
				}
			}

			cg_param = cgGetNextParameter(cg_param);
		}

		is_validate_ = true;
		for (size_t i = 0; i < ST_NumShaderTypes; ++ i)
		{
			is_validate_ &= is_shader_validate_[i];
		}
	}

	ShaderObjectPtr OGLShaderObject::Clone(RenderEffect& effect)
	{
		OGLShaderObjectPtr ret = MakeSharedPtr<OGLShaderObject>();

		ret->shader_descs_ = shader_descs_;
		ret->shader_text_ = shader_text_;
		ret->profiles_ = profiles_;

		ret->tex_sampler_binds_.resize(tex_sampler_binds_.size());
		for (size_t i = 0; i < tex_sampler_binds_.size(); ++ i)
		{
			ret->tex_sampler_binds_[i].first = tex_sampler_binds_[i].first;
			ret->tex_sampler_binds_[i].second.first = effect.ParameterByName(*(tex_sampler_binds_[i].second.first->Name()));
			ret->tex_sampler_binds_[i].second.second = effect.ParameterByName(*(tex_sampler_binds_[i].second.second->Name()));
		}

		OGLRenderFactory& render_factory(*checked_cast<OGLRenderFactory*>(&Context::Instance().RenderFactoryInstance()));
		for (size_t i = 0; i < ST_NumShaderTypes; ++ i)
		{
			ret->is_shader_validate_[i] = true;

			ret->shaders_[i] = cgCreateProgram(render_factory.CGContext(),
					CG_SOURCE, ret->shader_text_->c_str(), ret->profiles_[i],
					(*ret->shader_descs_)[i].func_name.c_str(), NULL);

			CGerror error;
			char const * err_string = cgGetLastErrorString(&error);
			if (error != CG_NO_ERROR)
			{
#ifdef KLAYGE_DEBUG
				std::cerr << *ret->shader_text_ << std::endl;
				std::cerr << err_string << std::endl;
				if (CG_COMPILER_ERROR == error)
				{
					std::cerr << cgGetLastListing(render_factory.CGContext()) << std::endl;
				}
#else
				UNREF_PARAM(err_string);
#endif

				ret->is_shader_validate_[i] = false;
			}

			cgGLLoadProgram(ret->shaders_[i]);

			ret->samplers_[i].resize(samplers_[i].size());

			CGparameter cg_param = cgGetFirstParameter(ret->shaders_[i], CG_GLOBAL);
			while (cg_param)
			{
				if (cgIsParameterUsed(cg_param, ret->shaders_[i])
					&& (CG_PARAMETERCLASS_OBJECT != cgGetParameterClass(cg_param)))
				{
					char const * pname = cgGetParameterName(cg_param);

					RenderEffectParameterPtr const & p = effect.ParameterByName(pname);
					if (p)
					{
						ret->param_binds_[i].push_back(ret->GetBindFunc(cg_param, p));
					}
					else
					{
						for (size_t j = 0; j < ret->tex_sampler_binds_.size(); ++ j)
						{
							if (ret->tex_sampler_binds_[j].first == pname)
							{
								parameter_bind_t new_pb;
								new_pb.combined_sampler_name = pname;
								new_pb.cg_param = cg_param;

								uint32_t index = cgGLGetTextureEnum(cg_param) - GL_TEXTURE0;
								BOOST_ASSERT(index < ret->samplers_[i].size());

								new_pb.func = SetOGLShaderParameter<std::pair<TexturePtr, SamplerStateObjectPtr> >(ret->samplers_[i][index], cg_param,
									index, ret->tex_sampler_binds_[j].second.first, ret->tex_sampler_binds_[j].second.second);

								ret->param_binds_[i].push_back(new_pb);
								break;
							}
						}
					}
				}

				cg_param = cgGetNextParameter(cg_param);
			}
		}

		ret->is_validate_ = true;
		for (size_t i = 0; i < ST_NumShaderTypes; ++ i)
		{
			ret->is_validate_ &= ret->is_shader_validate_[i];
		}

		return ret;
	}

	OGLShaderObject::parameter_bind_t OGLShaderObject::GetBindFunc(CGparameter cg_param, RenderEffectParameterPtr const & param)
	{
		parameter_bind_t ret;
		ret.param = param;
		ret.cg_param = cg_param;

		switch (param->type())
		{
		case REDT_bool:
			if (param->ArraySize() != 0)
			{
				ret.func = SetOGLShaderParameter<bool*>(cg_param, param);
			}
			else
			{
				ret.func = SetOGLShaderParameter<bool>(cg_param, param);
			}
			break;

		case REDT_dword:
		case REDT_int:
			if (param->ArraySize() != 0)
			{
				ret.func = SetOGLShaderParameter<int*>(cg_param, param);
			}
			else
			{
				ret.func = SetOGLShaderParameter<int>(cg_param, param);
			}
			break;

		case REDT_float:
			if (param->ArraySize() != 0)
			{
				ret.func = SetOGLShaderParameter<float*>(cg_param, param);
			}
			else
			{
				ret.func = SetOGLShaderParameter<float>(cg_param, param);
			}
			break;

		case REDT_float2:
			ret.func = SetOGLShaderParameter<float2>(cg_param, param);
			break;

		case REDT_float3:
			ret.func = SetOGLShaderParameter<float3>(cg_param, param);
			break;

		case REDT_float4:
			if (param->ArraySize() != 0)
			{
				ret.func = SetOGLShaderParameter<float4*>(cg_param, param);
			}
			else
			{
				ret.func = SetOGLShaderParameter<float4>(cg_param, param);
			}
			break;

		case REDT_float4x4:
			if (param->ArraySize() != 0)
			{
				ret.func = SetOGLShaderParameter<float4x4*>(cg_param, param);
			}
			else
			{
				ret.func = SetOGLShaderParameter<float4x4>(cg_param, param);
			}
			break;

		default:
			BOOST_ASSERT(false);
			break;
		}

		return ret;
	}

	void OGLShaderObject::Bind()
	{
		for (int i = 0; i < ST_NumShaderTypes; ++ i)
		{
			BOOST_FOREACH(BOOST_TYPEOF(param_binds_[i])::reference pb, param_binds_[i])
			{
				pb.func();
			}

			cgGLBindProgram(shaders_[i]);
			cgGLEnableProfile(profiles_[i]);

			BOOST_FOREACH(BOOST_TYPEOF(param_binds_[i])::reference pb, param_binds_[i])
			{
				if (!pb.param)
				{
					cgGLEnableTextureParameter(pb.cg_param);
				}
			}
		}
	}

	void OGLShaderObject::Unbind()
	{
		for (int i = 0; i < ST_NumShaderTypes; ++ i)
		{
			BOOST_FOREACH(BOOST_TYPEOF(param_binds_[i])::reference pb, param_binds_[i])
			{
				if (!pb.param)
				{
					cgGLDisableTextureParameter(pb.cg_param);
				}
			}

			cgGLUnbindProgram(profiles_[i]);
		}
	}
}
