/*
 * Copyright 2011-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "bgfx_p.h"

namespace bgfx
{
	namespace
	{
		struct ShaderBinaryReader
		{
			ShaderBinaryReader(const void* _data, uint32_t _size)
				: m_ptr( (const uint8_t*)_data)
				, m_end( (const uint8_t*)_data + _size)
			{
			}

			template<typename Ty>
			bool read(Ty& _value)
			{
				if (size_t(m_end - m_ptr) < sizeof(Ty) )
				{
					return false;
				}

				_value = bx::loadUnaligned<Ty>(m_ptr);
				m_ptr += sizeof(Ty);
				return true;
			}

			const uint8_t* readBytes(uint32_t _size)
			{
				if (size_t(m_end - m_ptr) < _size)
				{
					return NULL;
				}

				const uint8_t* ptr = m_ptr;
				m_ptr += _size;
				return ptr;
			}

			uint32_t getRemaining() const
			{
				return uint32_t(m_end - m_ptr);
			}

			const uint8_t* m_ptr;
			const uint8_t* m_end;
		};

		bool isShaderNameValid(const char* _name, uint32_t _len)
		{
			if (0 == _len)
			{
				return false;
			}

			const char ch = _name[0];
			if (!bx::isAlpha(ch)
			&&  '_' != ch)
			{
				return false;
			}

			for (uint32_t ii = 1; ii < _len; ++ii)
			{
				if (!bx::isAlphaNum(_name[ii])
				&&  '_' != _name[ii])
				{
					return false;
				}
			}

			return true;
		}
	} // namespace

	bool validateShaderBinary(const void* _data, uint32_t _size)
	{
		if (NULL == _data)
		{
			return false;
		}

		ShaderBinaryReader reader(_data, _size);

		uint32_t magic;
		if (!reader.read(magic)
		||  !isShaderBin(magic) )
		{
			return false;
		}

		const uint8_t version = uint8_t(magic>>24);
		if (version < 12
		||  version > kShaderBinVersion)
		{
			return false;
		}

		uint32_t hashIn;
		if (!reader.read(hashIn) )
		{
			return false;
		}

		if (version >= 6)
		{
			uint32_t hashOut;
			if (!reader.read(hashOut) )
			{
				return false;
			}
		}

		uint32_t rawSrvMask;
		uint32_t rawUavMask;
		if (!reader.read(rawSrvMask)
		||  !reader.read(rawUavMask) )
		{
			return false;
		}

		uint16_t count;
		if (!reader.read(count)
		||  count > BGFX_CONFIG_MAX_UNIFORMS)
		{
			return false;
		}

		const uint8_t* names[BGFX_CONFIG_MAX_UNIFORMS];
		uint16_t nameLengths[BGFX_CONFIG_MAX_UNIFORMS];
		bool predefinedSeen[PredefinedUniform::Count] = {};

		char name[256];

		for (uint32_t ii = 0; ii < count; ++ii)
		{
			uint8_t nameSize;
			if (!reader.read(nameSize)
			||  0 == nameSize)
			{
				return false;
			}

			const uint8_t* nameData = reader.readBytes(nameSize);
			if (NULL == nameData)
			{
				return false;
			}

			bx::memCopy(name, nameData, nameSize);
			name[nameSize] = '\0';

			if (!isShaderNameValid(name, nameSize) )
			{
				return false;
			}

			for (uint32_t jj = 0; jj < ii; ++jj)
			{
				if (nameLengths[jj] == nameSize
				&&  0 == bx::memCmp(names[jj], name, nameSize) )
				{
					return false;
				}
			}

			names[ii]       = nameData;
			nameLengths[ii] = nameSize;

			const PredefinedUniform::Enum predefined = nameToPredefinedUniformEnum(name);
			if (PredefinedUniform::Count != predefined)
			{
				if (predefinedSeen[predefined] )
				{
					return false;
				}

				predefinedSeen[predefined] = true;
			}

			uint8_t type;
			if (!reader.read(type) )
			{
				return false;
			}

			const uint8_t uniformType = type & ~kUniformMask;
			if (uniformType > UniformType::Mat4)
			{
				return false;
			}

			uint16_t num = 0;
			if (version < 14)
			{
				uint8_t num8;
				if (!reader.read(num8) )
				{
					return false;
				}
				num = num8;
			}
			else if (!reader.read(num) )
			{
				return false;
			}

			uint16_t regIndex;
			uint16_t regCount;
			if (!reader.read(regIndex)
			||  !reader.read(regCount) )
			{
				return false;
			}

			if (version >= 8)
			{
				uint8_t texComponent;
				uint8_t texDimension;
				if (!reader.read(texComponent)
				||  !reader.read(texDimension) )
				{
					return false;
				}

				if (texDimension > TextureDimension::Count)
				{
					return false;
				}

				if (TextureComponentType::Count == idToTextureComponentType(texComponent) )
				{
					return false;
				}
			}

			if (version >= 10)
			{
				uint16_t texFormat;
				if (!reader.read(texFormat) )
				{
					return false;
				}
			}

			BX_UNUSED(num, regIndex, regCount);
		}

		uint32_t shaderSize;
		if (!reader.read(shaderSize)
		||  uint64_t(shaderSize) + 1 > reader.getRemaining() )
		{
			return false;
		}

		return true;
	}

	struct DescriptorTypeToId
	{
		DescriptorType::Enum type;
		uint16_t id;
	};

	static constexpr DescriptorTypeToId s_descriptorTypeToId[] =
	{
		// NOTICE:
		// DescriptorType must be in order how it appears in DescriptorType::Enum! id is
		// unique and should not be changed if new DescriptorTypes are added.
		{ DescriptorType::StorageBuffer, 0x0007 },
		{ DescriptorType::StorageImage,  0x0003 },
	};
	static_assert(BX_COUNTOF(s_descriptorTypeToId) == DescriptorType::Count);
	static_assert(bx::isSorted(s_descriptorTypeToId, BX_COUNTOF(s_descriptorTypeToId), [](const DescriptorTypeToId& _lhs, const DescriptorTypeToId& _rhs) { return _lhs.type <= _rhs.type; }) );

	DescriptorType::Enum idToDescriptorType(uint16_t _id)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_descriptorTypeToId); ++ii)
		{
			if (s_descriptorTypeToId[ii].id == _id)
			{
				return s_descriptorTypeToId[ii].type;
			}
		}

		return DescriptorType::Count;
	}

	uint16_t descriptorTypeToId(DescriptorType::Enum _type)
	{
		return s_descriptorTypeToId[_type].id;
	}

	struct TextureComponentTypeToId
	{
		TextureComponentType::Enum type;
		uint8_t id;
	};

	static constexpr TextureComponentTypeToId s_textureComponentTypeToId[] =
	{
		// see comment in s_descriptorTypeToId
		{ TextureComponentType::Float,             0x00 },
		{ TextureComponentType::Int,               0x01 },
		{ TextureComponentType::Uint,              0x02 },
		{ TextureComponentType::Depth,             0x03 },
		{ TextureComponentType::UnfilterableFloat, 0x04 },
	};
	static_assert(BX_COUNTOF(s_textureComponentTypeToId) == TextureComponentType::Count);
	static_assert(bx::isSorted(s_textureComponentTypeToId, BX_COUNTOF(s_textureComponentTypeToId), [](const TextureComponentTypeToId& _lhs, const TextureComponentTypeToId& _rhs) { return _lhs.type <= _rhs.type; }) );

	TextureComponentType::Enum idToTextureComponentType(uint8_t _id)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_textureComponentTypeToId); ++ii)
		{
			if (s_textureComponentTypeToId[ii].id == _id)
			{
				return s_textureComponentTypeToId[ii].type;
			}
		}

		return TextureComponentType::Count;
	}

	uint8_t textureComponentTypeToId(TextureComponentType::Enum _type)
	{
		return s_textureComponentTypeToId[_type].id;
	}

	struct TextureDimensionToId
	{
		TextureDimension::Enum dimension;
		uint8_t id;
	};

	static constexpr TextureDimensionToId s_textureDimensionToId[] =
	{
		// see comment in s_descriptorTypeToId
		{ TextureDimension::Dimension1D,        0x01 },
		{ TextureDimension::Dimension2D,        0x02 },
		{ TextureDimension::Dimension2DArray,   0x03 },
		{ TextureDimension::DimensionCube,      0x04 },
		{ TextureDimension::DimensionCubeArray, 0x05 },
		{ TextureDimension::Dimension3D,        0x06 },
		{ TextureDimension::Dimension2DMS,      0x07 },
		{ TextureDimension::Dimension2DMSArray, 0x08 },
	};
	static_assert(BX_COUNTOF(s_textureDimensionToId) == TextureDimension::Count);
	static_assert(bx::isSorted(s_textureDimensionToId, BX_COUNTOF(s_textureDimensionToId), [](const TextureDimensionToId& _lhs, const TextureDimensionToId& _rhs) { return _lhs.dimension <= _rhs.dimension; }) );

	TextureDimension::Enum idToTextureDimension(uint8_t _id)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_textureDimensionToId); ++ii)
		{
			if (s_textureDimensionToId[ii].id == _id)
			{
				return s_textureDimensionToId[ii].dimension;
			}
		}

		return TextureDimension::Count;
	}

	uint8_t textureDimensionToId(TextureDimension::Enum _dim)
	{
		return s_textureDimensionToId[_dim].id;
	}

} // namespace bgfx
