// SPDX-License-Identifier: MS-PL

#pragma once

#include <any>
#include <memory>
#include <string>
#include <utility>

#include "System/ComponentModel/PropertyDescriptor.hpp"
#include "System/EventArgs.hpp"
#include "System/Type.hpp"

namespace CNA::Internal::Design {

template<class TComponent, class TValue>
class FieldPropertyDescriptor final : public System::ComponentModel::PropertyDescriptor {
public:
    /**
     * @brief Creates a descriptor for a public value-type field.
     *
     * @param name The XNA property name.
     * @param member The corresponding C++ field.
     */
    FieldPropertyDescriptor(std::string name, TValue TComponent::* member)
        : PropertyDescriptor(std::move(name)), member_(member) {}

    /** @brief Returns the boxed component type. @return The component type. */
    [[nodiscard]] System::Type getComponentTypeProperty() const override {
        return System::Type::From<TComponent>();
    }

    /** @brief Reports that the field can be edited. @return Always @c false. */
    [[nodiscard]] bool getIsReadOnlyProperty() const override { return false; }

    /** @brief Returns the boxed field type. @return The field type. */
    [[nodiscard]] System::Type getPropertyTypeProperty() const override {
        return System::Type::From<TValue>();
    }

    /** @brief Reports that the field has no reset value. @param component The boxed component. @return Always @c false. */
    [[nodiscard]] bool CanResetValue(const std::any& component) const override {
        (void)component;
        return false;
    }

    /** @brief Reads the field. @param component The boxed component. @return The boxed field value. */
    [[nodiscard]] std::any GetValue(const std::any& component) const override {
        return std::any(std::any_cast<const TComponent&>(component).*member_);
    }

    /** @brief Leaves the field unchanged because no reset value exists. @param component The boxed component. */
    void ResetValue(std::any& component) const override { (void)component; }

    /** @brief Writes the field. @param component The boxed component. @param value The boxed field value. */
    void SetValue(std::any& component, const std::any& value) const override {
        std::any_cast<TComponent&>(component).*member_ = std::any_cast<TValue>(value);
        OnValueChanged(component, System::EventArgs::Empty);
    }

    /** @brief Reports that the field is serializable. @param component The boxed component. @return Always @c true. */
    [[nodiscard]] bool ShouldSerializeValue(const std::any& component) const override {
        (void)component;
        return true;
    }

private:
    TValue TComponent::* member_;
};

template<class TComponent, class TValue>
class AccessorPropertyDescriptor final : public System::ComponentModel::PropertyDescriptor {
public:
    /** @brief Member-function type used to read a property. */
    using Getter = TValue (TComponent::*)() const;
    /** @brief Member-function type used to write a property. */
    using Setter = void (TComponent::*)(TValue);

    /**
     * @brief Creates a descriptor for a getter/setter property.
     *
     * @param name The XNA property name.
     * @param getter The getter member function.
     * @param setter The setter member function, or null for a read-only property.
     */
    AccessorPropertyDescriptor(std::string name, Getter getter, Setter setter)
        : PropertyDescriptor(std::move(name)), getter_(getter), setter_(setter) {}

    /** @brief Returns the boxed component type. @return The component type. */
    [[nodiscard]] System::Type getComponentTypeProperty() const override {
        return System::Type::From<TComponent>();
    }

    /** @brief Reports whether no setter was supplied. @return @c true for a read-only property. */
    [[nodiscard]] bool getIsReadOnlyProperty() const override { return setter_ == nullptr; }

    /** @brief Returns the boxed property type. @return The property type. */
    [[nodiscard]] System::Type getPropertyTypeProperty() const override {
        return System::Type::From<TValue>();
    }

    /** @brief Reports that the property has no reset value. @param component The boxed component. @return Always @c false. */
    [[nodiscard]] bool CanResetValue(const std::any& component) const override {
        (void)component;
        return false;
    }

    /** @brief Reads the property. @param component The boxed component. @return The boxed property value. */
    [[nodiscard]] std::any GetValue(const std::any& component) const override {
        const TComponent& typed = std::any_cast<const TComponent&>(component);
        return std::any((typed.*getter_)());
    }

    /** @brief Leaves the property unchanged because no reset value exists. @param component The boxed component. */
    void ResetValue(std::any& component) const override { (void)component; }

    /** @brief Writes the property. @param component The boxed component. @param value The boxed property value. */
    void SetValue(std::any& component, const std::any& value) const override {
        TComponent& typed = std::any_cast<TComponent&>(component);
        (typed.*setter_)(std::any_cast<TValue>(value));
        OnValueChanged(component, System::EventArgs::Empty);
    }

    /** @brief Reports that the property is serializable. @param component The boxed component. @return Always @c true. */
    [[nodiscard]] bool ShouldSerializeValue(const std::any& component) const override {
        (void)component;
        return true;
    }

private:
    Getter getter_;
    Setter setter_;
};

template<class TComponent, class TValue>
/**
 * @brief Creates a shared descriptor for a public field.
 *
 * @param name The XNA property name.
 * @param member The corresponding C++ field.
 * @return The property descriptor.
 */
[[nodiscard]] std::shared_ptr<System::ComponentModel::PropertyDescriptor>
MakeFieldProperty(std::string name, TValue TComponent::* member) {
    return std::make_shared<FieldPropertyDescriptor<TComponent, TValue>>(std::move(name), member);
}

template<class TComponent, class TValue>
/**
 * @brief Creates a shared descriptor for a getter/setter property.
 *
 * @param name The XNA property name.
 * @param getter The getter member function.
 * @param setter The setter member function.
 * @return The property descriptor.
 */
[[nodiscard]] std::shared_ptr<System::ComponentModel::PropertyDescriptor>
MakeAccessorProperty(std::string name,
                     typename AccessorPropertyDescriptor<TComponent, TValue>::Getter getter,
                     typename AccessorPropertyDescriptor<TComponent, TValue>::Setter setter) {
    return std::make_shared<AccessorPropertyDescriptor<TComponent, TValue>>(
        std::move(name), getter, setter);
}

} // namespace CNA::Internal::Design
