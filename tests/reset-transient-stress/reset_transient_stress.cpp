/*
 * Copyright 2011-2026 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include <bgfx/bgfx.h>

#include <bx/allocator.h>
#include <bx/mutex.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{
	constexpr bgfx::ViewId kRenderView   = 0;
	constexpr bgfx::ViewId kReadbackView = 1;
	constexpr uint32_t kWidth  = 128;
	constexpr uint32_t kHeight = 128;
	constexpr uint32_t kQuadVertices = 6;
	constexpr uint32_t kQuadIndices  = 6;

	const uint32_t kClearColor[2] =
	{
		0x203040ff,
		0xc06020ff,
	};

	struct PosColorVertex
	{
		float m_x;
		float m_y;
		float m_z;
		uint32_t m_abgr;
	};

	static const PosColorVertex s_quadVertices[kQuadVertices] =
	{
		{ 0.0f, -1.0f, 0.0f, 0xff00ff00 },
		{ 1.0f, -1.0f, 0.0f, 0xff00ff00 },
		{ 1.0f,  1.0f, 0.0f, 0xff00ff00 },
		{ 0.0f, -1.0f, 0.0f, 0xff00ff00 },
		{ 1.0f,  1.0f, 0.0f, 0xff00ff00 },
		{ 0.0f,  1.0f, 0.0f, 0xff00ff00 },
	};

	static const uint16_t s_quadIndices[kQuadIndices] =
	{
		0, 1, 2,
		3, 4, 5,
	};

	bgfx::VertexLayout s_layout;

	struct OpLog
	{
		static OpLog* s_self;

		OpLog()
			: m_file(NULL)
		{
			s_self = this;
		}

		~OpLog()
		{
			if (NULL != m_file)
			{
				fclose(m_file);
			}

			s_self = NULL;
		}

		void open(const char* _path)
		{
			if (NULL != _path
			&&  '\0' != *_path)
			{
				m_file = fopen(_path, "wb");
			}
		}

		void mark(const char* _format, ...)
		{
			char line[512];
			va_list argList;
			va_start(argList, _format);
			vsnprintf(line, sizeof(line), _format, argList);
			va_end(argList);

			bx::MutexScope scope(m_mutex);
			m_steps.emplace_back(line);

			if (NULL != m_file)
			{
				fprintf(m_file, "OP %s\n", line);
				fflush(m_file);
			}
		}

		void checkpoint(const char* _format, ...)
		{
			char line[512];
			va_list argList;
			va_start(argList, _format);
			vsnprintf(line, sizeof(line), _format, argList);
			va_end(argList);

			bx::MutexScope scope(m_mutex);
			m_steps.clear();

			if (NULL != m_file)
			{
				fprintf(m_file, "CHECKPOINT %s\n", line);
				fflush(m_file);
			}
		}

		void dump(const char* _reason)
		{
			bx::MutexScope scope(m_mutex);

			fprintf(stderr, "\nMINIMAL OPERATION SEQUENCE\n");
			fprintf(stderr, "reason: %s\n", _reason);

			if (m_steps.empty() )
			{
				fprintf(stderr, "  (no operations recorded since last checkpoint)\n");
			}

			for (const std::string& step : m_steps)
			{
				fprintf(stderr, "  %s\n", step.c_str() );
			}

			if (NULL != m_file)
			{
				fprintf(m_file, "FAIL %s\n", _reason);
				for (const std::string& step : m_steps)
				{
					fprintf(m_file, "FAILSTEP %s\n", step.c_str() );
				}
				fflush(m_file);
			}
		}

		bx::Mutex m_mutex;
		std::vector<std::string> m_steps;
		FILE* m_file;
	};

	OpLog* OpLog::s_self = NULL;

	struct TrackingAllocator : public bx::AllocatorI
	{
		TrackingAllocator()
			: m_liveBlocks(0)
			, m_maxBlocks(0)
			, m_allocCalls(0)
			, m_freeCalls(0)
		{
		}

		virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _filePath, uint32_t _line) override
		{
			void* ptr = m_allocator.realloc(_ptr, _size, _align, _filePath, _line);

			bx::MutexScope scope(m_mutex);
			if (NULL == _ptr
			&&  0 != _size)
			{
				++m_liveBlocks;
				++m_allocCalls;
				m_maxBlocks = m_liveBlocks > m_maxBlocks ? m_liveBlocks : m_maxBlocks;
			}
			else if (NULL != _ptr
			     &&  0 == _size)
			{
				--m_liveBlocks;
				++m_freeCalls;
			}

			return ptr;
		}

		int64_t liveBlocks()
		{
			bx::MutexScope scope(m_mutex);
			return m_liveBlocks;
		}

		int64_t maxBlocks()
		{
			bx::MutexScope scope(m_mutex);
			return m_maxBlocks;
		}

		int64_t allocCalls()
		{
			bx::MutexScope scope(m_mutex);
			return m_allocCalls;
		}

		int64_t freeCalls()
		{
			bx::MutexScope scope(m_mutex);
			return m_freeCalls;
		}

		bx::DefaultAllocator m_allocator;
		bx::Mutex m_mutex;
		int64_t m_liveBlocks;
		int64_t m_maxBlocks;
		int64_t m_allocCalls;
		int64_t m_freeCalls;
	};

	struct QuietCallback : public bgfx::CallbackI
	{
		virtual void fatal(const char* _filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char* _str) override
		{
			char reason[512];
			snprintf(reason, sizeof(reason), "fatal 0x%08x at %s:%u: %s", _code, _filePath, _line, _str);
			if (NULL != OpLog::s_self)
			{
				OpLog::s_self->dump(reason);
			}
			fprintf(stderr, "BGFX FATAL: %s\n", reason);
			abort();
		}

		virtual void traceVargs(const char*, uint16_t, const char*, va_list) override
		{
		}

		virtual void profilerBegin(const char*, uint32_t, const char*, uint16_t) override
		{
		}

		virtual void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override
		{
		}

		virtual void profilerEnd() override
		{
		}

		virtual uint32_t cacheReadSize(uint64_t) override
		{
			return 0;
		}

		virtual bool cacheRead(uint64_t, void*, uint32_t) override
		{
			return false;
		}

		virtual void cacheWrite(uint64_t, const void*, uint32_t) override
		{
		}

		virtual void screenShot(const char*, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void*, uint32_t, bool) override
		{
		}

		virtual void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override
		{
		}

		virtual void captureEnd() override
		{
		}

		virtual void captureFrame(const void*, uint32_t) override
		{
		}
	};

	uint64_t hashPixels(const uint32_t* _pixels, uint32_t _numPixels)
	{
		uint64_t hash = UINT64_C(1469598103934665603);
		const uint8_t* bytes = reinterpret_cast<const uint8_t*>(_pixels);
		const uint32_t numBytes = _numPixels * sizeof(uint32_t);

		for (uint32_t ii = 0; ii < numBytes; ++ii)
		{
			hash ^= bytes[ii];
			hash *= UINT64_C(1099511628211);
		}

		return hash;
	}

	const char* getShaderDir(bgfx::RendererType::Enum _type)
	{
		switch (_type)
		{
		case bgfx::RendererType::Noop:
		case bgfx::RendererType::Direct3D11: return "dxbc";
		case bgfx::RendererType::Direct3D12: return "dxil";
		case bgfx::RendererType::Agc:
		case bgfx::RendererType::Gnm:        return "pssl";
		case bgfx::RendererType::Metal:      return "metal";
		case bgfx::RendererType::OpenGL:     return "glsl";
		case bgfx::RendererType::OpenGLES:   return "essl";
		case bgfx::RendererType::Vulkan:     return "spirv";
		case bgfx::RendererType::WebGPU:     return "wgsl";
		default:                             return NULL;
		}
	}

	bool readFile(const char* _path, std::vector<uint8_t>& _data)
	{
		FILE* file = fopen(_path, "rb");
		if (NULL == file)
		{
			return false;
		}

		fseek(file, 0, SEEK_END);
		const long size = ftell(file);
		fseek(file, 0, SEEK_SET);

		if (0 >= size)
		{
			fclose(file);
			return false;
		}

		_data.resize(size_t(size) );
		const bool ok = size_t(size) == fread(_data.data(), 1, size_t(size), file);
		fclose(file);
		return ok;
	}

	bgfx::ShaderHandle loadShader(const char* _shaderRoot, const char* _dir, const char* _name)
	{
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s/%s.bin", _shaderRoot, _dir, _name);

		std::vector<uint8_t> data;
		if (!readFile(path, data) )
		{
			fprintf(stderr, "Failed to read shader: %s\n", path);
			return BGFX_INVALID_HANDLE;
		}

		const bgfx::Memory* mem = bgfx::alloc(uint32_t(data.size() + 1) );
		memcpy(mem->data, data.data(), data.size() );
		mem->data[data.size()] = '\0';
		return bgfx::createShader(mem);
	}

	bgfx::ProgramHandle loadProgram(const char* _shaderRoot, const char* _dir)
	{
		bgfx::ShaderHandle vsh = loadShader(_shaderRoot, _dir, "vs_cubes");
		bgfx::ShaderHandle fsh = loadShader(_shaderRoot, _dir, "fs_cubes");

		if (!bgfx::isValid(vsh)
		||  !bgfx::isValid(fsh) )
		{
			if (bgfx::isValid(vsh) )
			{
				bgfx::destroy(vsh);
			}
			if (bgfx::isValid(fsh) )
			{
				bgfx::destroy(fsh);
			}
			return BGFX_INVALID_HANDLE;
		}

		return bgfx::createProgram(vsh, fsh, true);
	}

	struct Test
	{
		Test()
			: m_frame(0)
			, m_resetToggle(0)
			, m_fbh(BGFX_INVALID_HANDLE)
			, m_fbhTexture(BGFX_INVALID_HANDLE)
			, m_readback(BGFX_INVALID_HANDLE)
			, m_vbh(BGFX_INVALID_HANDLE)
			, m_ibh(BGFX_INVALID_HANDLE)
			, m_program(BGFX_INVALID_HANDLE)
			, m_exhaustions(0)
			, m_duplicateSubmits(0)
			, m_invalidSubmits(0)
			, m_resets(0)
		{
			m_pixels.resize(kWidth*kHeight);
			memset(m_baselineHash, 0, sizeof(m_baselineHash) );
			memset(m_baselineLeft, 0, sizeof(m_baselineLeft) );
			memset(m_baselineRight, 0, sizeof(m_baselineRight) );
		}

		bool init(bgfx::RendererType::Enum _type, const char* _shaderRoot, TrackingAllocator* _allocator, QuietCallback* _callback)
		{
			bgfx::Init init;
			init.type      = _type;
			init.allocator = _allocator;
			init.callback  = _callback;
			init.swapChain.width  = 0;
			init.swapChain.height = 0;
			init.limits.numDrawCallPeakFrames = 1;
			init.limits.minTransientVbSize    = 1024;
			init.limits.minTransientIbSize    = 128;

			if (!bgfx::init(init) )
			{
				fprintf(stderr, "bgfx::init failed\n");
				return false;
			}

			const char* dir = getShaderDir(bgfx::getRendererType() );
			if (NULL == dir)
			{
				fprintf(stderr, "Unsupported renderer type: %d\n", bgfx::getRendererType() );
				return false;
			}

			m_program = loadProgram(_shaderRoot, dir);
			if (!bgfx::isValid(m_program) )
			{
				fprintf(stderr, "Failed to load vs_cubes/fs_cubes from %s/%s\n", _shaderRoot, dir);
				return false;
			}

			s_layout
				.begin()
				.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
				.end()
				;

			m_fbh = bgfx::createFrameBuffer(kWidth, kHeight, bgfx::TextureFormat::BGRA8);
			m_readback = bgfx::createTexture2D(
				  kWidth
				, kHeight
				, false
				, 1
				, bgfx::TextureFormat::BGRA8
				, BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK
				);

			if (!bgfx::isValid(m_fbh)
			||  !bgfx::isValid(m_readback) )
			{
				fprintf(stderr, "Failed to create offscreen frame buffer/readback texture\n");
				return false;
			}

			m_fbhTexture = bgfx::getTexture(m_fbh);
			if (!bgfx::isValid(m_fbhTexture) )
			{
				fprintf(stderr, "Failed to get frame buffer color texture\n");
				return false;
			}

			m_vbh = bgfx::createVertexBuffer(
				  bgfx::makeRef(s_quadVertices, sizeof(s_quadVertices) )
				, s_layout
				);
			m_ibh = bgfx::createIndexBuffer(
				bgfx::makeRef(s_quadIndices, sizeof(s_quadIndices) )
				);

			if (!bgfx::isValid(m_vbh)
			||  !bgfx::isValid(m_ibh) )
			{
				fprintf(stderr, "Failed to create static quad buffers\n");
				return false;
			}

			return true;
		}

		void shutdown()
		{
			if (bgfx::isValid(m_ibh) )
			{
				bgfx::destroy(m_ibh);
			}
			if (bgfx::isValid(m_vbh) )
			{
				bgfx::destroy(m_vbh);
			}
			if (bgfx::isValid(m_program) )
			{
				bgfx::destroy(m_program);
			}
			if (bgfx::isValid(m_readback) )
			{
				bgfx::destroy(m_readback);
			}
			if (bgfx::isValid(m_fbh) )
			{
				bgfx::destroy(m_fbh);
			}

			bgfx::shutdown();
		}

		void configureView(uint32_t _variant, bool _bindFrameBuffer = true)
		{
			bgfx::setViewRect(kRenderView, 0, 0, kWidth, kHeight);
			if (_bindFrameBuffer)
			{
				bgfx::setViewFrameBuffer(kRenderView, m_fbh);
			}
			bgfx::setViewClear(
				  kRenderView
				, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
				, kClearColor[_variant & 1]
				, 1.0f
				, 0
				);

			const float identity[16] =
			{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f,
			};
			bgfx::setViewTransform(kRenderView, identity, identity);
		}

		bool allocTransientQuad(bgfx::TransientVertexBuffer* _tvb, bgfx::TransientIndexBuffer* _tib)
		{
			if (kQuadVertices != bgfx::getAvailTransientVertexBuffer(kQuadVertices, s_layout)
			||  kQuadIndices  != bgfx::getAvailTransientIndexBuffer(kQuadIndices, false) )
			{
				return false;
			}

			bgfx::allocTransientVertexBuffer(_tvb, kQuadVertices, s_layout);
			bgfx::allocTransientIndexBuffer(_tib, kQuadIndices, false);

			memcpy(_tvb->data, s_quadVertices, sizeof(s_quadVertices) );
			memcpy(_tib->data, s_quadIndices, sizeof(s_quadIndices) );
			return true;
		}

		void submitStatic(uint8_t _discard)
		{
			const float identity[16] =
			{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f,
			};
			bgfx::setTransform(identity);
			bgfx::setVertexBuffer(0, m_vbh);
			bgfx::setIndexBuffer(m_ibh);
			bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
			bgfx::submit(kRenderView, m_program, 0, _discard);
		}

		void submitTransient(const bgfx::TransientVertexBuffer& _tvb, const bgfx::TransientIndexBuffer& _tib, uint8_t _discard)
		{
			const float identity[16] =
			{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f,
			};
			bgfx::setTransform(identity);
			bgfx::setVertexBuffer(0, &_tvb);
			bgfx::setIndexBuffer(&_tib);
			bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
			bgfx::submit(kRenderView, m_program, 0, _discard);
		}

		uint32_t nextResetFlags()
		{
			return 0 == (m_resetToggle++ & 1)
				? BGFX_RESET_VSYNC
				: BGFX_RESET_NONE
				;
		}

		bool readBack(uint32_t& _hash)
		{
			bgfx::blit(
				  kReadbackView
				, bgfx::TextureRegion{ m_readback }
				, bgfx::TextureRegion{ m_fbhTexture }
				);

			m_frame = bgfx::frame();
			const uint32_t expectedFrame = bgfx::read(
				  bgfx::TextureRegion{ m_readback }
				, m_pixels.data()
				);

			while (m_frame < expectedFrame)
			{
				m_frame = bgfx::frame();
			}

			_hash = hashPixels(m_pixels.data(), uint32_t(m_pixels.size() ) );
			return true;
		}

		bool captureBaseline(uint32_t _variant)
		{
			for (uint32_t ii = 0; ii < 4; ++ii)
			{
				configureView(_variant);
				submitStatic(BGFX_DISCARD_ALL);

				uint32_t hash;
				readBack(hash);

				const uint32_t left  = m_pixels[kWidth/4 + (kHeight/2)*kWidth];
				const uint32_t right = m_pixels[(kWidth*3)/4 + (kHeight/2)*kWidth];

				if (0 == ii)
				{
					m_baselineHash[_variant]  = hash;
					m_baselineLeft[_variant]  = left;
					m_baselineRight[_variant] = right;
				}
				else if (hash != m_baselineHash[_variant]
				     ||  left != m_baselineLeft[_variant]
				     ||  right != m_baselineRight[_variant] )
				{
					fprintf(stderr, "Non-deterministic baseline for variant %u\n", _variant);
					return false;
				}
			}

			if (m_baselineLeft[_variant] == m_baselineRight[_variant])
			{
				fprintf(stderr, "Baseline pattern did not draw: left == right\n");
				return false;
			}

			return true;
		}

		bool exhaustTransientBuffers()
		{
			constexpr uint32_t kPressureVertices = 65;
			constexpr uint32_t kPressureIndices  = 65;
			constexpr uint32_t kGuard = 1<<14;
			uint32_t iters = 0;

			while (getAvailVB(kPressureVertices) == kPressureVertices
			    || getAvailIB(kPressureIndices)  == kPressureIndices)
			{
				if (getAvailVB(kPressureVertices) == kPressureVertices)
				{
					bgfx::TransientVertexBuffer tvb;
					bgfx::allocTransientVertexBuffer(&tvb, kPressureVertices, s_layout);
				}

				if (getAvailIB(kPressureIndices) == kPressureIndices)
				{
					bgfx::TransientIndexBuffer tib;
					bgfx::allocTransientIndexBuffer(&tib, kPressureIndices, false);
				}

				if (++iters >= kGuard)
				{
					fprintf(stderr, "Transient exhaustion guard reached (%u)\n", kGuard);
					return false;
				}
			}

			if (getAvailVB(kQuadVertices) != 0
			||  getAvailIB(kQuadIndices)  != 0)
			{
				fprintf(stderr, "Transient buffers are not exhausted\n");
				return false;
			}

			++m_exhaustions;
			return true;
		}

		uint32_t getAvailVB(uint32_t _num)
		{
			return bgfx::getAvailTransientVertexBuffer(_num, s_layout);
		}

		uint32_t getAvailIB(uint32_t _num)
		{
			return bgfx::getAvailTransientIndexBuffer(_num, false);
		}

		bool runIteration(uint32_t _iteration, bool _exhaust)
		{
			OpLog::s_self->mark("iter %u: begin (variant=%u, exhaust=%d)", _iteration, _iteration&1, _exhaust ? 1 : 0);

			bgfx::TransientVertexBuffer tvb;
			bgfx::TransientIndexBuffer tib;
			if (!allocTransientQuad(&tvb, &tib) )
			{
				OpLog::s_self->mark("iter %u: allocTransientQuad", _iteration);
				return fail("transient quad allocation failed before pressure");
			}

			if (_exhaust)
			{
				if (!exhaustTransientBuffers() )
				{
					OpLog::s_self->mark("iter %u: exhaustTransientBuffers", _iteration);
					return fail("transient exhaustion failed");
				}
				OpLog::s_self->mark("iter %u: transient VB/IB exhausted", _iteration);
			}

			const uint32_t phase = _iteration & 3;
			if (0 == phase)
			{
				const uint32_t flags = nextResetFlags();
				OpLog::s_self->mark("iter %u: reset before draw flags=0x%08x", _iteration, flags);
				bgfx::reset(flags);
				++m_resets;
			}

			configureView(_iteration);
			OpLog::s_self->mark("iter %u: configure view", _iteration);

			OpLog::s_self->mark("iter %u: submit transient draw #1", _iteration);
			submitTransient(tvb, tib, BGFX_DISCARD_NONE);
			++m_duplicateSubmits;

			if (1 == phase)
			{
				const uint32_t flags = nextResetFlags();
				OpLog::s_self->mark("iter %u: reset after submit #1 flags=0x%08x", _iteration, flags);
				bgfx::reset(flags);
				++m_resets;
			}

			OpLog::s_self->mark("iter %u: duplicate submit", _iteration);
			submitTransient(tvb, tib, BGFX_DISCARD_ALL);
			++m_duplicateSubmits;

			if (2 == phase)
			{
				const uint32_t flags = nextResetFlags();
				OpLog::s_self->mark("iter %u: reset after duplicate submit flags=0x%08x", _iteration, flags);
				bgfx::reset(flags);
				++m_resets;
			}

			OpLog::s_self->mark("iter %u: submit invalid program", _iteration);
			bgfx::submit(kRenderView, BGFX_INVALID_HANDLE);
			++m_invalidSubmits;

			OpLog::s_self->mark("iter %u: submit invalid occlusion query", _iteration);
			bgfx::submit(kRenderView, m_program, BGFX_INVALID_HANDLE);
			++m_invalidSubmits;

			if (3 == phase)
			{
				const uint32_t flags = nextResetFlags();
				OpLog::s_self->mark("iter %u: reset after invalid submits flags=0x%08x", _iteration, flags);
				bgfx::reset(flags);
				++m_resets;
			}

			configureView(_iteration);
			OpLog::s_self->mark("iter %u: rebind view after reset", _iteration);

			OpLog::s_self->mark("iter %u: recovery submit", _iteration);
			if (getAvailVB(kQuadVertices) == kQuadVertices
			&&  getAvailIB(kQuadIndices)  == kQuadIndices)
			{
				submitTransient(tvb, tib, BGFX_DISCARD_ALL);
			}
			else
			{
				submitStatic(BGFX_DISCARD_ALL);
			}

			uint32_t hash = 0;
			OpLog::s_self->mark("iter %u: frame + readback", _iteration);
			readBack(hash);

			const uint32_t left  = m_pixels[kWidth/4 + (kHeight/2)*kWidth];
			const uint32_t right = m_pixels[(kWidth*3)/4 + (kHeight/2)*kWidth];

			if (hash != m_baselineHash[_iteration&1]
			||  left != m_baselineLeft[_iteration&1]
			||  right != m_baselineRight[_iteration&1])
			{
				OpLog::s_self->mark(
					  "iter %u: mismatch hash=%016llx expected=%016llx left=%08x/%08x right=%08x/%08x"
					, _iteration
					, (unsigned long long)hash
					, (unsigned long long)m_baselineHash[_iteration&1]
					, left, m_baselineLeft[_iteration&1]
					, right, m_baselineRight[_iteration&1]
					);
				return fail("framebuffer did not recover after reset");
			}

			if (_exhaust)
			{
				const uint32_t availVb = getAvailVB(kQuadVertices);
				const uint32_t availIb = getAvailIB(kQuadIndices);
				OpLog::s_self->mark("iter %u: post-frame transients vb=%u ib=%u", _iteration, availVb, availIb);

				if (kQuadVertices != availVb
				||  kQuadIndices  != availIb)
				{
					return fail("transient buffers did not recover after frame");
				}
			}

			OpLog::s_self->checkpoint("iter %u: hash=%016llx frame=%u", _iteration, (unsigned long long)hash, m_frame);
			return true;
		}

		bool fail(const char* _reason)
		{
			fprintf(stderr, "FAIL: %s\n", _reason);
			OpLog::s_self->dump(_reason);
			return false;
		}

		uint32_t m_frame;
		uint32_t m_resetToggle;
		bgfx::FrameBufferHandle m_fbh;
		bgfx::TextureHandle m_fbhTexture;
		bgfx::TextureHandle m_readback;
		bgfx::VertexBufferHandle m_vbh;
		bgfx::IndexBufferHandle m_ibh;
		bgfx::ProgramHandle m_program;
		uint64_t m_baselineHash[2];
		uint32_t m_baselineLeft[2];
		uint32_t m_baselineRight[2];
		std::vector<uint32_t> m_pixels;
		uint32_t m_exhaustions;
		uint32_t m_duplicateSubmits;
		uint32_t m_invalidSubmits;
		uint32_t m_resets;
	};

	enum class Fault
	{
		None,
		ResetNoRebind,
		DoubleDestroy,
		Leak,
	};

	struct Options
	{
		Options()
			: iterations(200)
			, exhaustEvery(1)
			, renderer(bgfx::RendererType::Count)
			, shaderRoot("examples/runtime/shaders")
			, traceFile(NULL)
			, fault(Fault::None)
		{
		}

		uint32_t iterations;
		uint32_t exhaustEvery;
		bgfx::RendererType::Enum renderer;
		const char* shaderRoot;
		const char* traceFile;
		Fault fault;
	};

	bool parseRenderer(const char* _arg, bgfx::RendererType::Enum& _type)
	{
		if (0 == strcmp(_arg, "auto") )    { _type = bgfx::RendererType::Count; return true; }
		if (0 == strcmp(_arg, "noop") )    { _type = bgfx::RendererType::Noop; return true; }
		if (0 == strcmp(_arg, "gl") )      { _type = bgfx::RendererType::OpenGL; return true; }
		if (0 == strcmp(_arg, "vulkan") )  { _type = bgfx::RendererType::Vulkan; return true; }
		return false;
	}

	bool parseFault(const char* _arg, Fault& _fault)
	{
		if (0 == strcmp(_arg, "none") )           { _fault = Fault::None; return true; }
		if (0 == strcmp(_arg, "reset-no-rebind") ) { _fault = Fault::ResetNoRebind; return true; }
		if (0 == strcmp(_arg, "double-destroy") )  { _fault = Fault::DoubleDestroy; return true; }
		if (0 == strcmp(_arg, "leak") )            { _fault = Fault::Leak; return true; }
		return false;
	}

	bool parseArgs(int _argc, char** _argv, Options& _options)
	{
		for (int32_t ii = 1; ii < _argc; ++ii)
		{
			if (0 == strcmp(_argv[ii], "--iterations")
			&&  ii+1 < _argc)
			{
				_options.iterations = uint32_t(atoi(_argv[++ii]) );
			}
			else if (0 == strcmp(_argv[ii], "--exhaust-every")
			     &&  ii+1 < _argc)
			{
				_options.exhaustEvery = uint32_t(atoi(_argv[++ii]) );
			}
			else if (0 == strcmp(_argv[ii], "--renderer")
			     &&  ii+1 < _argc)
			{
				const char* renderer = _argv[++ii];
				if (!parseRenderer(renderer, _options.renderer) )
				{
					fprintf(stderr, "Unknown renderer: %s\n", renderer);
					return false;
				}
			}
			else if (0 == strcmp(_argv[ii], "--shader-root")
			     &&  ii+1 < _argc)
			{
				_options.shaderRoot = _argv[++ii];
			}
			else if (0 == strcmp(_argv[ii], "--trace")
			     &&  ii+1 < _argc)
			{
				_options.traceFile = _argv[++ii];
			}
			else if (0 == strcmp(_argv[ii], "--fault")
			     &&  ii+1 < _argc)
			{
				const char* fault = _argv[++ii];
				if (!parseFault(fault, _options.fault) )
				{
					fprintf(stderr, "Unknown fault: %s\n", fault);
					return false;
				}
			}
			else
			{
				fprintf(stderr, "Unknown argument: %s\n", _argv[ii]);
				return false;
			}
		}

		return 0 != _options.iterations;
	}
}

int main(int _argc, char** _argv)
{
	Options options;
	if (!parseArgs(_argc, _argv, options) )
	{
		fprintf(
			  stderr
			, "usage: %s [--iterations N] [--exhaust-every N] [--renderer auto|noop|gl|vulkan]\n"
			  "          [--shader-root DIR] [--trace FILE]\n"
			  "          [--fault none|reset-no-rebind|double-destroy|leak]\n"
			, _argv[0]
			);
		return EXIT_FAILURE;
	}

	OpLog log;
	log.open(options.traceFile);

	TrackingAllocator allocator;
	QuietCallback callback;
	Test test;

	if (!test.init(options.renderer, options.shaderRoot, &allocator, &callback) )
	{
		test.shutdown();
		return EXIT_FAILURE;
	}

	const bool ok = test.captureBaseline(0)
		&&           test.captureBaseline(1)
		;

	if (!ok)
	{
		test.shutdown();
		return EXIT_FAILURE;
	}

	log.checkpoint("baseline captured frame=%u", test.m_frame);

	if (Fault::ResetNoRebind == options.fault)
	{
		log.mark("fault reset-no-rebind: reset");
		bgfx::reset(BGFX_RESET_VSYNC);
		log.mark("fault reset-no-rebind: clear variant 0 without setViewFrameBuffer");
		test.configureView(0, false);
		test.submitStatic(BGFX_DISCARD_ALL);

		uint32_t hash = 0;
		log.mark("fault reset-no-rebind: frame + readback");
		test.readBack(hash);
		if (hash == test.m_baselineHash[0])
		{
			fprintf(stderr, "FAULT NOT DETECTED: framebuffer content is unchanged\n");
			test.shutdown();
			return EXIT_FAILURE;
		}

		char reason[256];
		snprintf(reason, sizeof(reason), "fault injected: reset without rebinding framebuffer (hash=%016llx expected=%016llx)", (unsigned long long)hash, (unsigned long long)test.m_baselineHash[0]);
		OpLog::s_self->dump(reason);
		test.shutdown();
		return EXIT_FAILURE;
	}

	if (Fault::DoubleDestroy == options.fault)
	{
		log.mark("fault double-destroy: first destroy static vertex buffer");
		bgfx::destroy(test.m_vbh);
		log.mark("fault double-destroy: second destroy static vertex buffer");
		bgfx::destroy(test.m_vbh);
		fprintf(stderr, "FAULT NOT DETECTED: double destroy was accepted\n");
		test.shutdown();
		return EXIT_FAILURE;
	}

	uint32_t completed = 0;
	for (uint32_t ii = 0; ii < options.iterations; ++ii)
	{
		const bool exhaust = 0 != options.exhaustEvery
			&& 0 == (ii % options.exhaustEvery)
			;

		if (!test.runIteration(ii, exhaust) )
		{
			test.shutdown();
			return EXIT_FAILURE;
		}

		++completed;
	}

	void* leak = NULL;
	if (Fault::Leak == options.fault)
	{
		log.mark("fault leak: allocate 64 bytes without free");
		leak = bx::alloc(&allocator, 64);
	}

	test.shutdown();

	const int64_t live = allocator.liveBlocks();
	if (0 != live)
	{
		char reason[256];
		snprintf(reason, sizeof(reason), "allocator leak: %lld live blocks (leak=%p)", (long long)live, leak);
		OpLog::s_self->dump(reason);
		fprintf(stderr, "FAIL: %s\n", reason);
		return EXIT_FAILURE;
	}

	printf(
		  "PASS iterations=%u resets=%u exhaustions=%u duplicate_submits=%u invalid_submits=%u\n"
		  "     alloc_blocks live=0 max=%lld allocs=%lld frees=%lld\n"
		  "     frame=%u\n"
		, completed
		, test.m_resets
		, test.m_exhaustions
		, test.m_duplicateSubmits
		, test.m_invalidSubmits
		, (long long)allocator.maxBlocks()
		, (long long)allocator.allocCalls()
		, (long long)allocator.freeCalls()
		, test.m_frame
		);

	return EXIT_SUCCESS;
}
