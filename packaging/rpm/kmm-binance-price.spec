%global debug_package %{nil}
%global _build_id_links none

Name:           kmm-binance-price
Version:        %{plugin_version}
Release:        1
Summary:        Binance securities price plugin for KMyMoney (Qt6)
License:        GPL-2.0-or-later
Source0:        source.tar.gz
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  kmymoney-devel
BuildRequires:  cmake(Qt6Test)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6XmlGui)
BuildRequires:  cmake(LibAlkimia6)
BuildRequires:  gmp-devel
Requires:       kmymoney = %{kmm_version}

%description
Native Qt6/KF6 plugin with Binance spot and ECB fiat reference prices.
Built for openSUSE Tumbleweed; not a generic Fedora/RHEL RPM.

%prep
%setup -q -n source

%build
bash packaging/build-project.sh configure . build
bash packaging/build-project.sh build . build

%check
bash packaging/build-project.sh test . build

%install
bash packaging/build-project.sh install . build %{buildroot}

%files
%license LICENSE
%{plugin_dir}/kmm_binance_price.so
