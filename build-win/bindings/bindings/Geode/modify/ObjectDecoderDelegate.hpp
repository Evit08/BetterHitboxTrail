#pragma once
#include <Geode/modify/Modify.hpp>
#include <Geode/modify/Field.hpp>
#include <cocos2d.h>
using namespace geode::modifier;
namespace geode::modifier {
    
    
	#ifndef GEODE_CONCEPT_CHECK_getDecodedObject
		#define GEODE_CONCEPT_CHECK_getDecodedObject
		GEODE_CONCEPT_FUNCTION_CHECK(getDecodedObject) 
	#endif


	template<class Der>
	struct ModifyDerive<Der, ObjectDecoderDelegate> : ModifyBase<ModifyDerive<Der, ObjectDecoderDelegate>> {
        using BaseModify = ModifyBase<ModifyDerive<Der, ObjectDecoderDelegate>>;
		using ModifyBase<ModifyDerive<Der, ObjectDecoderDelegate>>::ModifyBase;
		using Base = ObjectDecoderDelegate;
        using Derived = Der;
		void apply() override {

			GEODE_APPLY_MODIFY_FOR_FUNCTION_ERROR_INLINE(ObjectDecoderDelegate, getDecodedObject, int, DS_Dictionary*)
		}
	};
}
